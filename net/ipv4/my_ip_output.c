#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/highmem.h>
#include <linux/slab.h>

#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>

#include <net/snmp.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/xfrm.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/arp.h>
#include <net/icmp.h>
#include <net/checksum.h>
#include <net/inetpeer.h>
#include <net/inet_ecn.h>
#include <net/lwtunnel.h>
#include <linux/bpf-cgroup.h>
#include <linux/igmp.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netfilter_bridge.h>
#include <linux/netlink.h>
#include <linux/tcp.h>

static void ip_if_init()
{
	struct net_device *dev;
	
	dev = dev_get_by_name(&init_net, "eth0");
	
	if (dev == NULL) 
	{
		printk("ip_if_init(): could't get the info on eth0\n");
		return -1;
    }
	
	// eth0のnet_deviceを取得できたら仮想IFに渡してIPIFと紐づける
	if (net_device_add_iface(dev) == 0) 
	{
		printk("ip_if_init(): successfully added eth0 to VIF\n");
    }
	
	return 0;
}

// void __init my_ip_init(void)
int __init my_ip_init(void)
{
	// IPスタックの初期化
	return ip_if_init();
}