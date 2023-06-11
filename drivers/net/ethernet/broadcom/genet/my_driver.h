



// デバイス固有の情報を置いておく
struct my_priv {
	
	// mmio用アドレス
	void __iomem *base;
	int irq;
	
};