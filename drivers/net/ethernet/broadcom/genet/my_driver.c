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

// Tx/Rx DMA register offset, skip 256 descriptors(引用)
#define WORDS_PER_BD(p)		(p->hw_params->words_per_bd)
#define DMA_DESC_SIZE		(WORDS_PER_BD(priv) * sizeof(u32))

#define GENET_RDMA_REG_OFF	(priv->hw_params->rdma_offset + \
				TOTAL_DESC * DMA_DESC_SIZE)

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

//　dma無効化処理
static u32 my_dma_disable(struct my_priv *priv)
{
	enum dma_reg reg_type;
	u32 reg;
	u32 dma_ctrl;
	
	// 読み込み処理関数を呼び出す
	// ベースアドレス + dma channel 2へのoffset + 受信リングバッファ分のoffset + 0x04(dmaコントローラ分のoffset)
	// の番地のbits状態を読み込む
	reg_type = DMA_CTRL;
 	dma_ctrl = my_readl(priv->base + GENET_RDMA_REG_OFF + DMA_RINGS_SIZE + my_dma_regs[reg_type]);
	reg &= ~dma_ctrl;
	// の番地にマスクしたbitsを書き込む
	my_readl(reg, priv->base + GENET_RDMA_REG_OFF + DMA_RINGS_SIZE + my_dma_regs[reg_type]);
	
 	return dma_ctrl;
}


// 受信ハンドラを用意する
static irqreturn_t my_isr(int irq, void *dev_id)
{
	// my_privへの型変換
	struct my_priv *priv = (struct my_priv *)dev_id;
	
	// 割り込みがあった
	printk("my_isr(): Hi there, there was an interrupt\n");
	printk("my_isr(): interrupt with an irq of %d\n", irq);
	printk("my_isr(): device with an irq of %d\n", priv->irq);
	
	return IRQ_HANDLED;
}

// .openハンドラを用意する
static int my_open(struct net_device *ndev)
{
	struct my_priv *priv = netdev_priv(ndev);
	int ret;
	unsigned long dma_ctrl;
	
	// dmaコントローラを無効化する
	// いったんコメントアウト
	dma_ctrl = my_dma_disable(priv);
	printk("my_open(): dma control register has bit state of %lu\n", dma_ctrl);

	// RXリングバッファ分のメモリを確保する
	// とりあえずまだ必要ないのでコメントアウトしておく
	// priv->rx_bds = priv->base + priv->hw_params->rdma_offset;
	// リングバッファのコントロールブロックの個数、256個用意する
	priv->num_rx_bds = TOTAL_DESC;
	// priv->num_rx_bds個分のバッファコントロールブロック(enet_cb)の配列を確保する
	priv->rx_cbs = kcalloc(priv->num_rx_bds, sizeof(struct enet_cb), GFP_KERNEL);
	if (!priv->rx_cbs) 
	{
		printk("my_open(): failed to allocate memory for RX ring buffer\n");
		return -ENOMEM;
	}
	
	// irqの登録を行う
 	ret = request_irq(priv->irq, my_isr, IRQF_SHARED, ndev->name, priv);
	if (ret < 0) 
	{
		printk("my_open(): failed to register my receive handler\n");
		return -1;
	}
	
	printk("my_open(): successfully registered my receive handler\n");
	
	// これは受信するだけなら呼ぶ必要はないはず？
	// netif_start_queue(dev);
	
	return 0;
		
}

// .stopハンドラを用意する
static int my_close(struct net_device *ndev)
{
	struct my_priv *priv = netdev_priv(ndev);
	
	// irqの解除を行う
	free_irq(priv->irq, priv);
	
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
	ndev = alloc_etherdev(sizeof(priv));
	if (ndev)
	{	
		printk("my_platform_device_probe(): successfully allocated net_device\n");
	} else {
		printk("my_platform_device_probe(): failed to allocate net_device\n");
		goto err;
	}
	
	// 返されたndevからprivを取得する、mmioやirqを投入してくため
	priv = netdev_priv(ndev);
	
	// 優先キューに紐づくirqをprivに投入する
	priv->irq = platform_get_irq(pdev, 1);
	if (priv->irq < 0)
	{
		printk("my_platform_device_probe(): failed to get irq from the device tree\n");
		goto err;
	} else {
		printk("my_platform_device_probe(): successfully got the irq of %d\n", priv->irq);
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
	
	// NICの初期化を行う
	oops = bcmgenet_mii_init(ndev);
	if (oops)
		goto err;
	
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
