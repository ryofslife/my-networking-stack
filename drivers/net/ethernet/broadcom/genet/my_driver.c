#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/if_ether.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/pm.h>
#include <linux/clk.h>
#include <net/arp.h>

#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/phy.h>
#include <linux/platform_data/bcmgenet.h>

#include <asm/unaligned.h>

// 既に自分のヘッダファイルに含まれているから必要ない？
// #include "bcmgenet.h"

#include "my_driver.h"

// intrl2レジスタブロックのレジスタの番地
#define INTRL2_CPU_STAT			0x00
#define INTRL2_CPU_SET			0x04
#define INTRL2_CPU_CLEAR		0x08
#define INTRL2_CPU_MASK_STATUS		0x0C
#define INTRL2_CPU_MASK_SET		0x10
#define INTRL2_CPU_MASK_CLEAR		0x14

// Tx/Rx DMA register offset, skip 256 descriptors
#define WORDS_PER_BD(p)		(p->hw_params->words_per_bd)
#define DMA_DESC_SIZE		(WORDS_PER_BD(priv) * sizeof(u32))

#define GENET_RDMA_REG_OFF	(priv->hw_params->rdma_offset + TOTAL_DESC * DMA_DESC_SIZE)

#define SKB_ALIGNMENT		32

#define RX_BUF_LENGTH		2048

#define GENET_MAX_MQ_CNT	4

// rx queue 16のバッファディスクリプタの数
#define GENET_Q16_RX_BD_CNT 256

#define DRIVER_NAME "RYOZ_DRIVER"

// デバイスspecificなパラメータを投入する
static struct my_hw_params *my_set_hw_params(struct my_hw_params *hw_params)
{	
	// メモリ確保する、せずにやたら悩んでた
	hw_params = kmalloc(sizeof(*hw_params), GFP_KERNEL);
	
	// 何のために用意する必要があるのか把握できたパラメータに関して都度追加する
	// https://github.com/raspberrypi/linux/blob/96110e96f1a82e236afb9a248258f1ef917766e9/drivers/net/ethernet/broadcom/genet/bcmgenet.c#L3758
	hw_params->rdma_offset = 0x2000;
	hw_params->words_per_bd = 3;
	hw_params->rx_queues = 0;
	// 優先ringのコントロールブロックは0とする
	hw_params->rx_bds_per_q = 0;
	
	return hw_params;
}


// 読み込み処理関数
static inline u32 my_readl(void __iomem *offset)
{
	// これどっちが呼び出されるの？
	// 把握して呼び出される方だけ残したい
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		return __raw_readl(offset);
	else
		return readl_relaxed(offset);
}

// 書き込み処理関数
static inline void my_writel(u32 value, void __iomem *offset) 
{
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
 		__raw_writel(value, offset);
 	else
 		writel_relaxed(value, offset);
}

// 受信リングの有効化と無効化を行う
static inline void my_rx_ring16_int_enable(struct my_rx_ring *ring)
{
	my_intrl2_0_writel(ring->priv, UMAC_IRQ_RXDMA_DONE, INTRL2_CPU_MASK_CLEAR);
}
static inline void my_rx_ring16_int_disable(struct my_rx_ring *ring)
{
	my_intrl2_0_writel(ring->priv, UMAC_IRQ_RXDMA_DONE, INTRL2_CPU_MASK_SET);
}
static inline void my_rx_ring_int_enable(struct my_rx_ring *ring)
{
	my_intrl2_1_writel(ring->priv, 1 << (UMAC_IRQ1_RX_INTR_SHIFT + ring->index), INTRL2_CPU_MASK_CLEAR);
}
static inline void my_rx_ring_int_disable(struct my_rx_ring *ring)
{
	my_intrl2_1_writel(ring->priv, 1 << (UMAC_IRQ1_RX_INTR_SHIFT + ring->index), INTRL2_CPU_MASK_SET);
}

// コントロールブロックに割り当てたリソースを解放する
static struct sk_buff *my_free_rx_cb(struct device *dev, struct enet_cb *cb)
{
	struct sk_buff *skb;

	skb = cb->skb;
	cb->skb = NULL;

	if (dma_unmap_addr(cb, dma_addr)) {
		dma_unmap_single(dev, dma_unmap_addr(cb, dma_addr), dma_unmap_len(cb, dma_len), DMA_FROM_DEVICE);
		dma_unmap_addr_set(cb, dma_addr, 0);
	}

	return skb;
}

// 受信バッファに割り当てたリソースを解放する
static void my_free_rx_buffers(struct my_priv *priv)
{
	struct sk_buff *skb;
	struct enet_cb *cb;
	int i;

	for (i = 0; i < priv->num_rx_bds; i++) {
		cb = &priv->rx_cbs[i];

		skb = my_free_rx_cb(&priv->pdev->dev, cb);
		if (skb)
			dev_consume_skb_any(skb);
	}
}

// 渡されたコントロールブロックdma_mappingedアドレスをdmaレジスタブロックに書き込んでいる
static inline void dmadesc_set_addr(struct my_priv *priv, void __iomem *d, dma_addr_t addr)
{
	my_writel(lower_32_bits(addr), d + DMA_DESC_ADDRESS_LO);
}

// 渡されたコントロールブロック用にskbを確保して割り当てる
static struct sk_buff *my_rx_refill(struct my_priv *priv, struct enet_cb *cb)
{
	struct device *kdev = &priv->pdev->dev;
	struct sk_buff *skb;
	struct sk_buff *rx_skb;
	dma_addr_t mapping;

	// skbを確保する
	// マクロが反映されていないので一時的に直接渡す
	printk(KERN_INFO "my_rx_refill(debug 1): %u\n", priv->rx_buf_len);
	skb = __netdev_alloc_skb(priv->ndev, priv->rx_buf_len + SKB_ALIGNMENT, GFP_ATOMIC);
	// skb = __netdev_alloc_skb(priv->ndev, 2048 + SKB_ALIGNMENT, GFP_ATOMIC);
	if (!skb) {
		printk(KERN_INFO "my_rx_refill(): could not allocated skb for the control block\n");
		return NULL;
	} else {
		printk(KERN_INFO "my_rx_refill(): allocated skb for the control block\n");
	}

	// 確保したskbのアドレスは論理アドレスである、そのままではdmaコントローラは扱えないのでdmableなアドレスに変換する
	printk(KERN_INFO "my_rx_refill(debug 2): %u\n", priv->rx_buf_len);
	mapping = dma_map_single(kdev, skb->data, priv->rx_buf_len, DMA_FROM_DEVICE);
	if (dma_mapping_error(kdev, mapping)) {
		dev_kfree_skb_any(skb);
		printk(KERN_INFO "my_rx_refill(): could not map the skb to dma address space\n");
		return NULL;
	} else {
		printk(KERN_INFO "my_rx_refill(): mapped the skb to dma address space\n");
	}

	// 現在cbに割り当てられているskbをunmapする、新たに確保したskbを割り当てるため
	printk(KERN_INFO "my_rx_refill(): debug\n");
	rx_skb = my_free_rx_cb(kdev, cb);

	// 新たに確保したskbをcbに割り当てる
	printk(KERN_INFO "my_rx_refill(): debug 1\n");
	cb->skb = skb;
	printk(KERN_INFO "my_rx_refill(): debug 2\n");
	dma_unmap_addr_set(cb, dma_addr, mapping);
	printk(KERN_INFO "my_rx_refill(): debug 3\n");
	dma_unmap_len_set(cb, dma_len, priv->rx_buf_len);
	printk(KERN_INFO "my_rx_refill(): debug 4\n");
	dmadesc_set_addr(priv, cb->bd_addr, mapping);
	printk(KERN_INFO "my_rx_refill(): debug 5\n");

	/* Return the current Rx skb to caller */
	return rx_skb;
}

// 渡されたリングに存在するコントロールブロックそれぞれにskbを確保する
static int my_alloc_rx_buffers(struct my_priv *priv, struct my_rx_ring *ring)
{
	struct enet_cb *cb;
	struct sk_buff *skb;
	int i;

	printk("my_alloc_rx_buffers(): allocating bffuer for ring %u\n", ring->index);
	printk("my_alloc_rx_buffers(): there are %u buffer descripters\n", ring->size);
	for (i = 0; i < ring->size; i++) {
		cb = ring->cbs + i;
		skb = my_rx_refill(priv, cb);
		if (skb)
			dev_consume_skb_any(skb);
		if (!cb->skb)
			return -ENOMEM;
	}

	return 0;
}

// coalescing engineのセットアップ
static void my_set_rx_coalesce(struct my_rx_ring *ring, u32 usecs, u32 pkts)
{
	struct my_priv *priv = ring->priv;
	unsigned int i = ring->index;
	u32 reg;

	my_rdma_ring_writel(priv, i, pkts, DMA_MBUF_DONE_THRESH);

	reg = my_rdma_readl(priv, DMA_RING0_TIMEOUT + i);
	reg &= ~DMA_TIMEOUT_MASK;
	reg |= DIV_ROUND_UP(usecs * 1000, 8192);
	my_rdma_writel(priv, reg, DMA_RING0_TIMEOUT + i);
}

// macの受信バッファを初期化する?
static void my_umac_reset(struct my_priv *priv)
{
	u32 reg;

	// 受信バッファコントロールレジスタの状態を取得する、関数はヘッダファイルに定義した
	reg = my_sys_readl(priv, SYS_RBUF_FLUSH_CTRL);

	// 取得したbitsへのマスクを用意する
	// 1bit目を1にセットする
	reg |= BIT(1);

	// 受信バッファコントロールレジスタに書き込む、こちらもヘッダファイルに定義してある
	my_sys_writel(priv, reg, SYS_RBUF_FLUSH_CTRL);
	udelay(10);

	// 1bit目以外を1にしてandする
	reg &= ~BIT(1);
	my_sys_writel(priv, reg, SYS_RBUF_FLUSH_CTRL);
	udelay(10);
}
static void reset_umac(struct my_priv *priv)
{
	// my_umac_resetでやっていることと何が違うのか、この辺りはデータシートで確認しないとわからないかも
	// たぶん受信バッファを解放している
	my_sys_writel(priv, 0, SYS_RBUF_FLUSH_CTRL);
	// introduce 10 micro seconds of delay
	udelay(10);

	// ドライバのコメントではsoftなリセットをしていると書いてある、それだけじゃいまいちわからない
	my_umac_writel(priv, CMD_SW_RESET, UMAC_CMD);
	// introduce 2 micro seconds of delay
	udelay(2);
}

// macを初期化する
static void init_umac(struct my_priv *priv)
{
	// レジスタの状態を取得してマスクしていく
	u32 reg;
	// phyのレジスタに対するマスク、たぶん
	u32 int0_enable = 0;

	reset_umac(priv);

	// 受信カウンタをリセットする
	my_umac_writel(priv, MIB_RESET_RX | MIB_RESET_RUNT, UMAC_MIB_CTRL);
	// 受信カウンタを初期化する
	my_umac_writel(priv, 0, UMAC_MIB_CTRL);

	// MTUサイズを設定する
	my_umac_writel(priv, ENET_MAX_MTU_SIZE, UMAC_MAX_FRAME_LEN); 

	// receive status blockを有効化する
	reg = my_rbuf_readl(priv, RBUF_CTRL);
	reg |= RBUF_64B_EN;
	my_rbuf_writel(priv, reg, RBUF_CTRL);

	// MDIOを有効化して、phyがmacに対して割り込みを行えるようにする
	int0_enable |= (UMAC_IRQ_MDIO_DONE | UMAC_IRQ_MDIO_ERROR);
	my_intrl2_0_writel(priv, int0_enable, INTRL2_CPU_MASK_CLEAR);

}

// 受信dma用リングの初期化
static int my_init_rx_ring(struct my_priv *priv, unsigned int index, unsigned int size, unsigned int start_ptr, unsigned int end_ptr)
{
    // リングの構造体の配列がヘッダファイルで宣言されている
    // リングそれぞれを初期化していく
	struct my_rx_ring *ring = &priv->rx_rings[index];
	u32 words_per_bd = WORDS_PER_BD(priv);
	int ret;

    // privのリングにさらにprivを持たせている
	ring->priv = priv;

    // 何番目のリングなのかを指定する
	ring->index = index;

    // リングごとに割り込みの有効化を行うハンドラ関数を指定している、これが割り込みを発生させている、たぶん
    // このハンドラ関数が呼ばれるタイミングを押さえておく必要がある
	if (index == DESC_INDEX) {
		ring->int_enable = my_rx_ring16_int_enable;
		ring->int_disable = my_rx_ring16_int_disable;
	} else {
		ring->int_enable = my_rx_ring_int_enable;
		ring->int_disable = my_rx_ring_int_disable;
	}

	// 対象リングの番地、priv->rx_cbsはリング共通
	ring->cbs = priv->rx_cbs + start_ptr;
	// 対象リングにおけるバッファディスクリプタ(bd)の数
	ring->size = size;
	// これはよくわからん
	ring->c_index = 0;
	// start_ptrは　i番目(ring) × 一つのringが持つdbの数　として定義している
	// 0番目のringからの距離
	ring->read_ptr = start_ptr;
	ring->cb_ptr = start_ptr;
	// end_ptrは　(i + 1)番目 × 一つのringが持つdbの数　として定義している
	// 対象リングのお尻の番地
	ring->end_ptr = end_ptr - 1;
	// coalescing engine周りのパラメータを定義する、ここをいじってみるのは面白いかも
	// bcmだとopen内で定義している
	ring->rx_coalesce_usecs = 50;
	ring->rx_max_coalesced_frames = 1;

	// リングのコントロールブロックにそれぞれskbを確保する
	// 上の一連のringのパラメータを投入する過程でoverflowしてそう
	printk("my_init_rx_ring(): each buffer within the ring with the size of %u\n", priv->rx_buf_len);
	ret = my_alloc_rx_buffers(priv, ring);
	if (ret)
		return ret;

	// 設定したパラメータに基づいてcoalescing engineをチューニング
	my_set_rx_coalesce(ring, ring->rx_coalesce_usecs, ring->rx_max_coalesced_frames);

	my_rdma_ring_writel(priv, index, 0, RDMA_PROD_INDEX);
	my_rdma_ring_writel(priv, index, 0, RDMA_CONS_INDEX);
	my_rdma_ring_writel(priv, index, ((size << DMA_RING_SIZE_SHIFT) | RX_BUF_LENGTH), DMA_RING_BUF_SIZE);
	my_rdma_ring_writel(priv, index, (DMA_FC_THRESH_LO << DMA_XOFF_THRESHOLD_SHIFT) | DMA_FC_THRESH_HI, RDMA_XON_XOFF_THRESH);
	//　start_ptr(end_ptr) * words_per_bd　をrdma_ringレジスタブロックの対象レジスタに書き込んでいる
	/* Set start and end address, read and write pointers */
	my_rdma_ring_writel(priv, index, start_ptr * words_per_bd, DMA_START_ADDR);
	my_rdma_ring_writel(priv, index, start_ptr * words_per_bd, RDMA_READ_PTR);
	my_rdma_ring_writel(priv, index, start_ptr * words_per_bd, RDMA_WRITE_PTR);
	my_rdma_ring_writel(priv, index, end_ptr * words_per_bd - 1, DMA_END_ADDR);

	return ret;
}

// 受信queueを初期化する
static int my_init_rx_queues(struct net_device *dev)
{
	struct my_priv *priv = netdev_priv(dev);
	enum dma_reg reg_type;
	u32 i;
	u32 dma_enable;
	u32 dma_ctrl, ring_cfg;
	int ret;

	// rdmaコントロールレジスタのbitsを取得する
	reg_type = DMA_CTRL;
	dma_ctrl = my_rdma_readl(priv, DMA_CTRL);
	dma_enable = dma_ctrl & DMA_EN;

	// rdmaコントロールレジスタのenable bitsをセットする
	my_rdma_writel(priv, dma_ctrl, DMA_CTRL);

	dma_ctrl = 0;
	ring_cfg = 0;

	/* Initialize Rx priority queues */
	// 1から15までのqueueを初期化する
	for (i = 0; i < priv->hw_params->rx_queues; i++) {
		ret = my_init_rx_ring(priv, i, priv->hw_params->rx_bds_per_q, i * priv->hw_params->rx_bds_per_q, (i + 1) * priv->hw_params->rx_bds_per_q);

		if (ret) {
			return ret;
		}

		ring_cfg |= (1 << i);
		dma_ctrl |= (1 << (i + DMA_RING_BUF_EN_SHIFT));
	}

	/* Initialize Rx default queue 16 */
	// デフォルトの16番目のringを初期化する
	ret = my_init_rx_ring(priv, DESC_INDEX, GENET_Q16_RX_BD_CNT, priv->hw_params->rx_queues * priv->hw_params->rx_bds_per_q, TOTAL_DESC);

	if (ret) {
		return ret;
	}

	// ここでこのマスクはどういう状態なのか？
	ring_cfg |= (1 << DESC_INDEX);
	dma_ctrl |= (1 << (DESC_INDEX + DMA_RING_BUF_EN_SHIFT));

	/* Enable rings */
	// control and statusレジスタに書き込む、受信リングバッファを有効化
	my_rdma_writel(priv, ring_cfg, DMA_RING_CFG);

	// re有効化している
	if (dma_enable) {
		dma_ctrl |= DMA_EN;
	}
	my_rdma_writel(priv, dma_ctrl, DMA_CTRL);

	return 0;
}

//　dmaを無効化する
static u32 my_disable_dma(struct my_priv *priv)
{
	enum dma_reg reg_type;
	u32 reg;
	u32 dma_ctrl;
	// u32 dbg;
	
	reg_type = DMA_CTRL;
	
	// 読み込み処理関数を呼び出す
	// ベースアドレス + dma channel 2へのoffset + 受信リングバッファ分のoffset + 0x04(dmaコントローラ分のoffset)
	// の番地のbits状態を読み込む
	dma_ctrl = 1 << (DESC_INDEX + DMA_RING_BUF_EN_SHIFT) | DMA_EN;

 	reg = my_readl(priv->base + GENET_RDMA_REG_OFF + DMA_RINGS_SIZE + my_dma_regs[reg_type]);
	reg &= ~dma_ctrl;
	printk("my_disable_dma(): the value written to the dma ctrl address is %u\n", reg);

	// の番地にマスクしたbitsを書き込む
	my_writel(reg, priv->base + GENET_RDMA_REG_OFF + DMA_RINGS_SIZE + my_dma_regs[reg_type]);

	// 書き込んだ値を読み込んでみる
	// dbg = my_readl(priv->base + GENET_RDMA_REG_OFF + DMA_RINGS_SIZE + my_dma_regs[reg_type]);
	// printk("my_disable_dma(): reading the value previously written to the dma ctrl address is %u\n", dbg);

	// 受信バッファの解放を行う
	reg = my_sys_readl(priv, SYS_RBUF_FLUSH_CTRL);
	my_sys_writel(priv, reg | BIT(0), SYS_RBUF_FLUSH_CTRL);
	udelay(10);
	my_sys_writel(priv, reg, SYS_RBUF_FLUSH_CTRL);
	udelay(10);
	
 	return dma_ctrl;
}

// dmaを初期化する
static int my_init_dma(struct my_priv *priv)
{
	int ret;
	unsigned int i;
	enum dma_reg reg_type;

	// コントロールブロック単体の構造体
	struct enet_cb *cb;

	/* Initialize common Rx ring structures */
	// リングバッファのコントロールブロックの個数、256個用意する
	priv->num_rx_bds = TOTAL_DESC;
	// num_rx_bds個分のバッファコントロールブロック(enet_cb)の配列を確保する
	priv->rx_cbs = kcalloc(priv->num_rx_bds, sizeof(struct enet_cb), GFP_KERNEL);
	if (!priv->rx_cbs) 
	{
		printk("my_init_dma(): failed to allocate memory for RX ring buffer\n");
		return -ENOMEM;
	}

	// それぞれのコントロールブロックの番地を指定する
	for (i = 0; i < priv->num_rx_bds; i++) {
		cb = priv->rx_cbs + i;
		cb->bd_addr = priv->rx_bds + i * DMA_DESC_SIZE;
	}

	/* Init rDma */
	// 受信dmaレジスタブロックのDMA_SCB_BURST_SIZEレジスタに書き込む
	reg_type = DMA_SCB_BURST_SIZE;
	my_writel(priv->dma_max_burst_length, priv->base + GENET_RDMA_REG_OFF + DMA_RINGS_SIZE + my_dma_regs[reg_type]);

	/* Initialize Rx queues */
 	// etherコントローラのdma・割り込みの有効化を行う
 	ret = my_init_rx_queues(priv->ndev);

	// 有効化に失敗した場合に確保したリングバッファ分のメモリを解放する
	if (ret) {
		printk("my_init_dma(): failed to initialize Rx queues\n");
		my_free_rx_buffers(priv);
		kfree(priv->rx_cbs);
		return ret;
	}

	return 0;
}

// dmaを有効化する
static void my_enable_dma(struct my_priv *priv, u32 dma_ctrl)
{
	enum dma_reg reg_type;
	u32 reg;

	reg_type = DMA_CTRL;

	printk("my_enable_dma(): the value of the mask is %u\n", dma_ctrl);

	reg = my_readl(priv->base + GENET_RDMA_REG_OFF + DMA_RINGS_SIZE + my_dma_regs[reg_type]);
	printk("my_enable_dma(): the value read from the dma ctrl address is %u\n", reg);

	reg |= dma_ctrl;
	printk("my_enable_dma(): the value written to the dma ctrl address is %u\n", reg);

	my_writel(reg, priv->base + GENET_RDMA_REG_OFF + DMA_RINGS_SIZE + my_dma_regs[reg_type]);

	reg = my_readl(priv->base + GENET_RDMA_REG_OFF + DMA_RINGS_SIZE + my_dma_regs[reg_type]);
	printk("my_enable_dma(): the value read from the dma ctrl address after written is %u\n", reg);

}

// 通常受信ハンドラを用意する
static irqreturn_t my_isr0(int irq, void *dev_id)
{
	// my_privへの型変換
	struct my_priv *priv = (struct my_priv *)dev_id;
	unsigned int status;
	
	// 割り込みがあった
	printk("my_isr0(): Hi there, there was an interrupt\n");
	printk("my_isr0(): interrupt with an irq of %d\n", irq);
	printk("my_isr0(): device with an irq of %d\n", priv->irq0);

	// 割り込みbitsを無効化する
	// しないと延々と割り込みが入り続ける現象が発生する、している
	status = my_intrl2_0_readl(priv, INTRL2_CPU_STAT) & ~my_intrl2_0_readl(priv, INTRL2_CPU_MASK_STATUS);
	my_intrl2_0_writel(priv, status, INTRL2_CPU_CLEAR);

	return IRQ_HANDLED;
}
// 優先受信ハンドラを用意する
static irqreturn_t my_isr1(int irq, void *dev_id)
{
	// my_privへの型変換
	struct my_priv *priv = (struct my_priv *)dev_id;
	unsigned int status;
	
	// 割り込みがあった
	printk("my_isr1(): Hi there, there was an interrupt\n");
	printk("my_isr1(): interrupt with an irq of %d\n", irq);
	printk("my_isr1(): device with an irq of %d\n", priv->irq0);
	
	// 割り込みbitsを無効化する
	// しないと延々と割り込みが入り続ける現象が発生する、している
	status = my_intrl2_1_readl(priv, INTRL2_CPU_STAT) & ~my_intrl2_1_readl(priv, INTRL2_CPU_MASK_STATUS);
	my_intrl2_1_writel(priv, status, INTRL2_CPU_CLEAR);

	return IRQ_HANDLED;
}

// 受信のモードをセットする
static void my_set_rx_mode(struct net_device *dev)
{
	struct my_priv *priv = netdev_priv(dev);
	u32 reg;

	reg = my_umac_readl(priv, UMAC_CMD);

	// promiscuosモードで受信する
	reg |= CMD_PROMISC;
	my_umac_writel(priv, reg, UMAC_CMD);
	my_umac_writel(priv, 0, UMAC_MDF_CTRL);
	return;

}

// 各リングごとに渡してある割り込み有効化ハンドラを呼び出して割り込みを有効化する
static void my_enable_rx(struct my_priv *priv)
{
	unsigned int i;
	struct my_rx_ring *ring;

	// 優先ringの有効化
	// my_init_rx_ringで渡した割り込み有効化ハンドラを呼び出して対象を渡す
	for (i = 0; i < priv->hw_params->rx_queues; ++i) {
		printk("my_enable_rx(): initializing the ring of %u\n", i);
		ring = &priv->rx_rings[i];
		ring->int_enable(ring);
	}

	// 通常リングの有効化
	ring = &priv->rx_rings[DESC_INDEX];
	ring->int_enable(ring);

}

// macの有効化
static void umac_enable_set(struct my_priv *priv, u32 mask, bool enable)
{
	u32 reg;

	// umacレジスタブロックのUMAC_CMDレジスタに書き込む
	reg = my_umac_readl(priv, UMAC_CMD);
	reg |= mask;
	my_umac_writel(priv, reg, UMAC_CMD);

}

// phy<->mac間の割り込みの有効化
static void my_link_intr_enable(struct my_priv *priv)
{
	u32 int0_enable = 0;

	// intrl2_0が通常、intrl2_1が優先である
	// intrl2_0レジスタブロックのINTRL2_CPU_MASK_CLEARレジスタに書き込む
	int0_enable |= UMAC_IRQ_LINK_EVENT;
	my_intrl2_0_writel(priv, int0_enable, INTRL2_CPU_MASK_CLEAR);
}

// 受信に必要なコンポーネントを有効化する
// エンジンをかける
static void my_netif_start(struct net_device *dev)
{
	struct my_priv *priv = netdev_priv(dev);

	// 受信モードをセットする、promiscで受信する
	my_set_rx_mode(dev);

	// 各リングごとに渡してある割り込み有効化ハンドラを呼び出して割り込みを有効化する
	my_enable_rx(priv);

	// macをrxに関して有効化する
	umac_enable_set(priv, CMD_RX_EN, true);

	// phy<->mac間の割り込みを有効化する、たぶん
	my_link_intr_enable(priv);

	// phyを有効化している、たぶん
	phy_start(dev->phydev);
}

// .openハンドラを用意する
static int my_open(struct net_device *ndev)
{
	struct my_priv *priv = netdev_priv(ndev);
	int ret;
	unsigned long dma_ctrl;
	char *isr0 = "warikomi0";
	char *isr1 = "warikomi1";

	// macをリセットする
	my_umac_reset(priv);

	// macの初期化から有効化？まで行う
	init_umac(priv);
	
	// dmaコントローラを無効化する
	dma_ctrl = my_disable_dma(priv);

	// dmaコントローラを初期化する
	ret = my_init_dma(priv);
	if (ret) {
		printk("my_open(): failed to initialize DMA\n");
		return -1;
	}
	
	// irqの登録を行う
 	ret = request_irq(priv->irq0, my_isr0, IRQF_SHARED, isr0, priv);
	if (ret < 0) 
	{
		printk("my_open(): failed to register my regular receive handler\n");
		return -1;
	}
 	ret = request_irq(priv->irq1, my_isr1, IRQF_SHARED, isr1, priv);
	if (ret < 0) 
	{
		printk("my_open(): failed to register my priority receive handler\n");
		return -1;
	}
	
	printk("my_open(): successfully registered my receive handler\n");

	// dmaコントローラを有効化する
	my_enable_dma(priv, dma_ctrl);
	
	// phy,mac,ringなどなど割り込みを発生させるのに必要なコンポーネントを有効化する
	my_netif_start(ndev);
	
	return 0;
		
}

// .stopハンドラを用意する
static int my_close(struct net_device *ndev)
{
	struct my_priv *priv = netdev_priv(ndev);
	
	// irqの解除を行う
	free_irq(priv->irq0, priv);
	free_irq(priv->irq1, priv);
	
	// .openで呼び出していないので、ここで呼ぶ必要はないはず
	// netif_stop_queue(dev);
	
	return 0;
	
}

// .ndo_start_xmitハンドラを用意する
static int my_xmit(struct sk_buff *skb, struct net_device *dev)
{
	// 特にまだ何もしない
	printk("my_xmit(): I'm not yet supposed to be called...\n");
	
	return 0;
	
}

// .ndo_gハンドラを用意する
static struct net_device_stats *my_get_stats(struct net_device *dev)
{
	// 特にまだ何もしない
	printk("my_get_stats(): Hi there, I'm the bookkeeper!\n");
	
	return &dev->stats;

}

// net_device_opsを定義する
static const struct net_device_ops my_netdev_ops = {
	.ndo_open		= my_open,
	.ndo_stop		= my_close,
	.ndo_start_xmit		= my_xmit,
	.ndo_get_stats		= my_get_stats,
};

// probe関数を用意する
static int my_platform_device_probe(struct platform_device *pdev)
{
	// 物理デバイス用
	struct my_priv *priv;
	// 仮想デバイス用
	struct net_device *ndev;
	// platform_get_resource()用
	struct resource *rsc;
	// ハード固有のパラメータ
	struct my_hw_params *hw_params;
	
	int oops;
	int ooops;
	
	printk("my_platform_device_probe(): the device being probed is %d\n", *pdev->name);
	
	// single queueで仮想デバイスをprovisionする
	// 以下二つ何が違うのか、調べる(todo)
	// ndev = alloc_etherdev_mqs(sizeof(priv), GENET_MAX_MQ_CNT + 1, GENET_MAX_MQ_CNT + 1);
	ndev = alloc_etherdev_mqs(sizeof(*priv), GENET_MAX_MQ_CNT + 1, GENET_MAX_MQ_CNT + 1);
	if (ndev)
	{	
		printk("my_platform_device_probe(): successfully allocated net_device\n");
	} else {
		printk("my_platform_device_probe(): failed to allocate net_device\n");
		goto err;
	}
	
	// 返されたndevからprivを取得する、mmioやirqを投入してくため
	priv = netdev_priv(ndev);

	// 通常キューに紐づくirqをprivに投入する
	priv->irq0 = platform_get_irq(pdev, 0);
	if (priv->irq0 < 0)
	{
		printk("my_platform_device_probe(): failed to get irq from the device tree\n");
		goto err;
	} else {
		printk("my_platform_device_probe(): successfully got the irq0 of %d\n", priv->irq0);
	}
	
	// 優先キューに紐づくirqをprivに投入する
	priv->irq1 = platform_get_irq(pdev, 1);
	if (priv->irq1 < 0)
	{
		printk("my_platform_device_probe(): failed to get irq from the device tree\n");
		goto err;
	} else {
		printk("my_platform_device_probe(): successfully got the irq1 of %d\n", priv->irq1);
	}
	
	// mmioのベースアドレスを取得、このタイミングではまだ物理アドレスなので仮想アドレスに変換する必要がある
	rsc = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!rsc) {
		printk("my_platform_device_probe(): failed to get the list of resources\n");
		goto err;
	} else {
		printk("my_platform_device_probe(): successfully got the list of resources\n");
	}
	
	// mmio用の物理アドレス領域を確保する
	if (!request_mem_region(rsc->start, rsc->end - rsc->start + 1, pdev->name)) 
	{
		printk("request_mem_region failed.\n");
		goto err;
    } else {
		printk("my_platform_device_probe(): successfully allocated mmio region");
	}
	
	// mmioの物理アドレスを仮想アドレスに変換する
	printk("my_platform_device_probe(): the mmio physical address is %lld\n", rsc->start);
	priv->base = ioremap(rsc->start, rsc->end - rsc->start + 1);
	if (!priv->base) {
		printk("my_platform_device_probe(): failed to get the mmio physical address\n");
		goto err;
	} else {
		printk("my_platform_device_probe(): successfully mapped the mmio virtual address of %p\n", priv->base);
	}
	
	// 仮想デバイスに対して物理デバイスを紐づける
	SET_NETDEV_DEV(ndev, &pdev->dev);
	// 物理デバイスに対して仮想デバイスを紐づける
	dev_set_drvdata(&pdev->dev, ndev);
	
	// privにhwパラメータを置いておく
	priv->hw_params = my_set_hw_params(hw_params);
	
	// opsとndevを紐づける
	ndev->netdev_ops = &my_netdev_ops;
	
	// privにndevとpdevを投入する
	priv->ndev = ndev;
	priv->pdev = pdev;
	
	// genetのversionは5のハズ、デバイスツリーからversionを取得して置いておく
	// device_nodeに対してcompatibleなMDIO bus node?を探索するのに必要ぽい
	priv->version = GENET_V5;
	
	// etherコントローラのphy-modeを吐かせる
	printk(KERN_INFO "phy-mode: %d\n", device_get_phy_mode(&pdev->dev));
	
	// mii起動するまでのqueueを初期化
	init_waitqueue_head(&priv->wq);

	// 各リングのコントロールブロック一つ分のサイズ
	priv->rx_buf_len = RX_BUF_LENGTH;
	printk("my_platform_device_probe(): assigned rx buff len: %u\n", priv->rx_buf_len);
	
	// NICの初期化を行う
	oops = bcmgenet_mii_init(ndev);
	if (oops)
		goto err;

	printk("my_platform_device_probe(): the number of rx queue was %u\n", priv->hw_params->rx_queues);

	// ndevをmutexする
	rtnl_lock();
	/* setup number of real queues  + 1 (GENET_V1 has 0 hardware queues
	 * just the ring 16 descriptor based TX
	 */
	// rx queueの数を指定する、これを呼ばないとrx_queuesは0のままである
	netif_set_real_num_rx_queues(priv->ndev, priv->hw_params->rx_queues + 1);
	rtnl_unlock();

	printk("my_platform_device_probe(): the number of rx queue is %u\n", ndev->real_num_rx_queues);
	priv->hw_params->rx_queues = ndev->real_num_rx_queues;
	printk("my_platform_device_probe(): the number of rx queue is %u\n", priv->hw_params->rx_queues);
	
	// 一通りできたら以下を呼ぶ
	ooops = register_netdev(ndev);
	if (ooops)
	{
		printk(KERN_INFO "my_platform_device_probe(): error registering ndev\n");
		goto err;
	}
	
	// 一連のprobeを完了
	printk(KERN_INFO "my_platform_device_probe(): successfully registered ndev\n");
	printk(KERN_INFO "my_platform_device_probe(): probing completed\n");
	
	return oops;
	
err:
	free_netdev(ndev);
	return -1;
	
}

// remove関数を用意する
static int my_platform_device_remove(struct platform_device *pdev)
{
	// 仮想デバイス分のメモリを解放する
	struct net_device *ndev = dev_get_drvdata(&pdev->dev);
	free_netdev(ndev);
	
    // 完了
    printk(KERN_INFO "my_platform_device_remove(): successfully removed the device\n");
	
    return 0;
}

// デバイスツリーにあるNICに対応するドライバであることを書く
static const struct of_device_id nic_match[] = {
	// { .compatible = "brcm,bcm2711-genet-v5" },
	// {},
	
	// { .compatible = "brcm,genet-v1" },
	// { .compatible = "brcm,genet-v2" },
	// { .compatible = "brcm,genet-v3" },
	// { .compatible = "brcm,genet-v4" },
	// { .compatible = "brcm,genet-v5" },
	{ .compatible = "brcm,bcm2711-genet-v5" },
	// { .compatible = "brcm,bcm7712-genet-v5" },
	{ },
};
MODULE_DEVICE_TABLE(of, nic_match);

// kernelに渡すためのハンドラ関数を用意する
static struct platform_driver my_platform_driver = {
	.probe = my_platform_device_probe,
	.remove = my_platform_device_remove,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = nic_match,
	},
};

module_platform_driver(my_platform_driver);

MODULE_AUTHOR("RYO");
MODULE_DESCRIPTION("RYOZ_DRIVER");
MODULE_LICENSE("GPL");
