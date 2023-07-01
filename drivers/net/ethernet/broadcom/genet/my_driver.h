#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/spinlock.h>
#include <linux/clk.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/phy.h>
#include <linux/dim.h>
#include <linux/ethtool.h>

#include "../unimac.h"

#include "bcmgenet.h"

// デバイスspecificなパラメータ
struct my_hw_params {
	// baseから始まるリングバッファブロック一つ分のデータ部の大きさ
	unsigned int rdma_offset;
	// とりあえず必要、まだ把握できていない
	unsigned int words_per_bd;
};

// DMA channel base番地からそれぞれのレジスタ番地へのoffset
// dma channel ２をNICは使用している
enum dma_reg {
	DMA_RING_CFG = 0,
	DMA_CTRL,
};
static const u8 my_dma_regs[] = {
	// control and statusレジスタ
	[DMA_RING_CFG]		= 0x00,
	// コントロールレジスタ
	[DMA_CTRL]		= 0x04,
};

// リングの構造体
struct my_rx_ring {
 	unsigned long	bytes;
 	unsigned long	packets;
 	unsigned long	errors;
 	unsigned long	dropped;
 	unsigned int	index;		/* Rx ring index */
 	struct enet_cb	*cbs;		/* Rx ring buffer control block */
 	unsigned int	size;		/* Rx ring size */
 	unsigned int	c_index;	/* Rx last consumer index */
 	unsigned int	read_ptr;	/* Rx ring read pointer */
 	unsigned int	cb_ptr;		/* Rx ring initial CB ptr */
 	unsigned int	end_ptr;	/* Rx ring end CB ptr */
 	unsigned int	old_discards;
 	u32		rx_max_coalesced_frames;
 	u32		rx_coalesce_usecs;
 	void (*int_enable)(struct my_rx_ring *);
 	void (*int_disable)(struct my_rx_ring *);
 	struct my_priv *priv;
};

// 自分が定義するデバイス固有の情報を置いておく
struct my_priv {
	
	// mmio用アドレス
	void __iomem *base;
	// 割り込み番号
	int irq;
	// probe時に投入する、pdevはkernelから渡されるalloc_etherdevに基づくデバイスの情報
	struct net_device *ndev;
	// probe時に投入する、pdevはkernelから渡されるデバイスツリーに基づくデバイスの情報
	struct platform_device *pdev;
	// phy-mode
	phy_interface_t phy_interface;
	// etherコントローラのversion、compatibleなmdioを探索する際に必要
	enum bcmgenet_version version;
	struct device_node *mdio_dn;
	// PHY phandle?
	struct device_node *phy_dn;
	// dma周り、RXリングバッファのブロック数を定義している
	unsigned int num_rx_bds;
	//　dma周り、RXリングバッファのブロックの構造を定義している
	struct enet_cb *rx_cbs;
	// デバイスspecificなパラメータ
	struct my_hw_params *hw_params;
	// リングの構造体を配列としてDESC_INDEX=16個分確保する、ハードにqueueが16ある、たぶん
	struct my_rx_ring rx_rings[DESC_INDEX + 1];
	
	// その他
	bool internal_phy;
	struct platform_device *mii_pdev;
	wait_queue_head_t wq;
	
};
									
// 自分用に再定義する、umac周りのレジスタの状態を取得する
static inline u32 my_umac_readl(struct my_priv *priv, u32 off)
{	
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN)) 
		return __raw_readl(priv->base + GENET_UMAC_OFF + off);		
	else								
		return readl_relaxed(priv->base + GENET_UMAC_OFF + off);	
}

// 自分用に再定義する、受信バッファコントロールレジスタの状態を取得する
// SYSレジスタブロックからの読み取りを行う
static inline u32 my_sys_readl(struct my_priv *priv, u32 off)
{	
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN)) 
		return __raw_readl(priv->base + GENET_SYS_OFF + off);		
	else								
		return readl_relaxed(priv->base + GENET_SYS_OFF + off);	
}
// SYSレジスタブロックへの書き込みを行う
static inline u32 my_sys_writel(struct my_priv *priv, u32 val, u32 off)
{									
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		__raw_writel(val, priv->base + GENET_SYS_OFF + off);	
	else								
		writel_relaxed(val, priv->base + GENET_SYS_OFF + off);		
}
// UMACレジスタブロックからの読み取りを行う
static inline u32 my_umac_readl(struct my_priv *priv, u32 off)
{	
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN)) 
		return __raw_readl(priv->base + GENET_UMAC_OFF + off);		
	else								
		return readl_relaxed(priv->base + GENET_UMAC_OFF + off);	
}
// UMACレジスタブロックへの書き込みを行う
static inline u32 my_umac_writel(struct my_priv *priv, u32 val, u32 off)
{									
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		__raw_writel(val, priv->base + GENET_UMAC_OFF + off);	
	else								
		writel_relaxed(val, priv->base + GENET_UMAC_OFF + off);		
}
// rbufレジスタブロックからの読み取りを行う
static inline u32 my_rbuf_readl(struct my_priv *priv, u32 off)
{	
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN)) 
		return __raw_readl(priv->base + GENET_RBUF_OFF + off);		
	else								
		return readl_relaxed(priv->base + GENET_RBUF_OFF + off);	
}
// rbufレジスタブロックへの書き込みを行う
static inline u32 my_rbuf_writel(struct my_priv *priv, u32 val, u32 off)
{									
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		__raw_writel(val, priv->base + GENET_RBUF_OFF + off);	
	else								
		writel_relaxed(val, priv->base + GENET_RBUF_OFF + off);		
}
// intrl2_0レジスタブロックからの読み取りを行う
static inline u32 my_intrl2_0_readl(struct my_priv *priv, u32 off)
{	
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN)) 
		return __raw_readl(priv->base + GENET_INTRL2_0_OFF + off);		
	else								
		return readl_relaxed(priv->base + GENET_INTRL2_0_OFF + off);	
}
// intrl2_0レジスタブロックへの書き込みを行う
static inline u32 my_intrl2_0_writel(struct my_priv *priv, u32 val, u32 off)
{									
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		__raw_writel(val, priv->base + GENET_INTRL2_0_OFF + off);	
	else								
		writel_relaxed(val, priv->base + GENET_INTRL2_0_OFF + off);		
}
// intrl2_1レジスタブロックからの読み取りを行う
static inline u32 my_intrl2_1_readl(struct my_priv *priv, u32 off)
{	
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN)) 
		return __raw_readl(priv->base + GENET_INTRL2_1_OFF + off);		
	else								
		return readl_relaxed(priv->base + GENET_INTRL2_1_OFF + off);	
}
// intrl2_1レジスタブロックへの書き込みを行う
static inline u32 my_intrl2_1_writel(struct my_priv *priv, u32 val, u32 off)
{									
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		__raw_writel(val, priv->base + GENET_INTRL2_1_OFF + off);	
	else								
		writel_relaxed(val, priv->base + GENET_INTRL2_1_OFF + off);		
}
// rdmaレジスタブロックからの読み取りを行う
static inline u32 my_rdma_readl(struct my_priv *priv, enum dma_reg reg_type)
{	
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN)) 
		return __raw_readl(priv->base + GENET_RDMA_REG_OFF + DMA_RINGS_SIZE + my_dma_regs[reg_type]);		
	else								
		return readl_relaxed(priv->base + GENET_RDMA_REG_OFF + DMA_RINGS_SIZE + my_dma_regs[reg_type]);	
}
// rdmaレジスタブロックへの書き込みを行う
static inline u32 my_rdma_writel(struct my_priv *priv, u32 val, enum dma_reg reg_type)
{									
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN))
		__raw_writel(val, priv->base + GENET_RDMA_REG_OFF + DMA_RINGS_SIZE + my_dma_regs[reg_type]);	
	else								
		writel_relaxed(val, priv->base + GENET_RDMA_REG_OFF + DMA_RINGS_SIZE + my_dma_regs[reg_type]);		
}