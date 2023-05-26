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

// n3t_device構造体についてはlinux/netdevice.hで定義してある
static struct n3t_device *n3xt;

// デバイスドライバが登録する仮想デバイス（net_device）を登録する
int add_n3t_device(struct net_device *dev)
{
	struct n3t_device *d3v;
	
	d3v = kmalloc(sizeof(*dev), GFP_KERNEL);
	
	if(!d3v) {
		printk(KERN_INFO "add_n3t_device(): error allocating memory to n3t_device");
		return -1;
	}
	
	// 仮想デバイスの名前を取得する
	memcpy(d3v->name, dev->name, IFNAMSIZ);
	
	// 仮想デバイスのMACアドレスを取得
	memcpy(d3v->dev_addr, dev->dev_addr, ETH_ALEN);
	
	// n3xtのアドレスを更新
	d3v->next = n3xt;
	n3xt = d3v;
	
	return 0;
}

// この関数はとりあえずデバイスドライバ(bcmgenet.c)から呼び出している
int n3t_device_add_ip_iface(char *dev_name, struct ip_iface *ipif)
{
	
	// とりあえずここまででip_iface構造体がデバイスドライバから受け取れているのか、仮想デバイスリストができているのかを確認する
	printk(KERN_INFO "net_device_add_iface(): target device name that which searching %s\n", n3xt->name);
	printk(KERN_INFO "net_device_add_iface(): ip unicast address of %u\n", ipif->unicast);
	printk(KERN_INFO "net_device_add_iface(): ip netmask of %u\n", ipif->netmask);
	printk(KERN_INFO "net_device_add_iface(): ip broadcast address of %u\n", ipif->broadcast);
	printk(KERN_INFO "net_device_add_iface(): resgistering %s\n", n3xt->name);
	printk(KERN_INFO "net_device_add_iface(): hw address(pM) of %pM\n", n3xt->dev_addr);
	
	// ここでip_ifaceのlinked listへの追加を行う
	// https://github.com/ryofslife/sample_network_stack/blob/main/net.c#L156
	
	// dev_nameによるdevリストの探索
	// https://github.com/ryofslife/sample_network_stack/blob/main/net.c#L148
	
	// 探索して見つかったdevをipifに対して登録する
	// https://github.com/ryofslife/sample_network_stack/blob/main/net.c#L141
	
	// 登録まで完了したら0を返す
	return 0;
}
