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


#define DRIVER_NAME "RYOZ_DRIVER"

// probe関数を用意する
static int my_platform_device_probe(struct platform_device *pdev)
{
	// 物理デバイス用
	struct bcmgenet_priv *priv;
	// 仮想デバイス用
	struct net_device *ndev;
	
	// single queueで仮想デバイスをprovisionする
	ndev = alloc_etherdev(sizeof(priv));
	
	// 仮想デバイスに対して物理デバイスを紐づける
	SET_NETDEV_DEV(ndev, &pdev->dev);
	// 物理デバイスに対して仮想デバイスを紐づける
	dev_set_drvdata(&pdev->dev, ndev);
	
	// 一通りできたら以下を呼ぶ
	// register_netdev()
	
	// 一連のprobeを完了
	printk(KERN_INFO "my_platform_device_probe(): probing completed\n");
	
	return 0;
	
}

// remove関数を用意する
static int my_platform_device_remove(struct platform_device *pdev)
{
	// 仮想デバイス分のメモリを解放する
	struct net_device *ndev = dev_get_drvdata(&pdev->dev);
	free_netdev(ndev);
	
    // 完了
    printk(KERN_INFO "my_platform_device_remove(): successfully removedthe device\n");
	
    return 0;
}

// デバイスツリーにあるNICに対応するドライバであることを書く
static const struct of_device_id nic_match[] = {
	{ .compatible = "brcm,bcm2711-genet-v5" },
	{},
};
MODULE_DEVICE_TABLE(of, nic_match);

// kernelに渡すためのハンドラ関数を用意する
static struct platform_driver my_platform_driver = {
	.probe = my_platform_device_probe,
	.remove = my_platform_device_remove,
	.driver = {
		.name = DRIVER_NAME,
	},
};

module_platform_driver(my_platform_driver);

MODULE_AUTHOR("RYO");
MODULE_DESCRIPTION("RYOZ_DRIVER");
MODULE_LICENSE("GPL");