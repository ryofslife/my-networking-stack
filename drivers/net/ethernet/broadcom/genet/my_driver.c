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

#include "bcmgenet.h"

#include "my_driver.h"


#define DRIVER_NAME "RYOZ_DRIVER"

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
	
	// irqの登録を行う
 	ret = request_irq(priv->irq, my_isr, IRQF_SHARED, ndev->name, priv);
	if (ret < 0) {
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
	
	// opsとndevを紐づける
	ndev->netdev_ops = &my_netdev_ops;
	
	// privにndevとpdevを投入する
	priv->dev = dev;
	priv->pdev = pdev;
	
	// etherコントローラのphy-modeを吐かせる
	printk(KERN_INFO "phy-mode: %d\n", device_get_phy_mode(&pdev->dev));
	
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
	
	printk(KERN_INFO "my_platform_device_probe(): successfully registered ndev\n");
	
	// 一連のprobeを完了
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
