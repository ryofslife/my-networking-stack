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
	// その他
	bool internal_phy;
	struct platform_device *mii_pdev;
	wait_queue_head_t wq;
	
};
									
// 自分用に再定義する、とりあえずこっちのヘッダファイルに置いておく
static inline u32 my_umac_readl(struct my_priv *priv, u32 off)
{	
	if (IS_ENABLED(CONFIG_MIPS) && IS_ENABLED(CONFIG_CPU_BIG_ENDIAN)) 
		return __raw_readl(priv->base + GENET_UMAC_OFF + off);		
	else								
		return readl_relaxed(priv->base + GENET_UMAC_OFF + off);	
}