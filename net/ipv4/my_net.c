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

#include <net/my_net.h>

// 仮想IF(net_iface)に対してnet_ifaceに対して仮想デバイスを登録する
int net_device_add_iface(struct net_device *dev)
{
	printk(KERN_INFO "net_device_add_iface(): resgistering %s\n", dev->name);
	printk(KERN_INFO "net_device_add_iface(): irq with %u\n", dev->irq);
	printk(KERN_INFO "net_device_add_iface(): type of %u\n", dev->type);
	printk(KERN_INFO "net_device_add_iface(): hw address(u) of %u\n", dev->dev_addr);
	printk(KERN_INFO "net_device_add_iface(): hw address(pM) of %pM\n", dev->dev_addr);
	
	return 0;
}

