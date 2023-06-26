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