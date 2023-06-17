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
	
};