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
static struct n3t_device *next_dev;
static struct ip_iface *next_ipif;

// デバイスドライバが登録する仮想デバイス（net_device）を登録する
int add_n3t_device(struct net_device *dev)
{
	struct n3t_device *d3v;
	
	d3v = kmalloc(sizeof(*d3v), GFP_KERNEL);
	// d3v = kmalloc(sizeof(*dev), GFP_KERNEL);
	
	if(!d3v) {
		printk(KERN_INFO "add_n3t_device(): error allocating memory to n3t_device");
		return -1;
	}
	
	// 仮想デバイスの名前を取得する
	memcpy(d3v->name, dev->name, IFNAMSIZ);
	
	// 仮想デバイスのMACアドレスを取得
	memcpy(d3v->dev_addr, dev->dev_addr, ETH_ALEN);
	
	// 渡されたデバイスをデバイスリストの先頭に追加する
	d3v->next = next_dev;
	next_dev = d3v;
	
	return 0;
}

// この関数はとりあえずデバイスドライバ(bcmgenet.c)から呼び出している
int n3t_device_add_ip_iface(char *dev_name, struct ip_iface *ipif)
{
	struct n3t_device *entry;
	
	// とりあえずここまででip_iface構造体がデバイスドライバから受け取れているのか、仮想デバイスリストができているのかを確認する
	printk(KERN_INFO "n3t_device_add_iface(): target device name that which searching %s\n", dev_name);
	printk(KERN_INFO "n3t_device_add_iface(): ip unicast address of %u\n", ipif->unicast);
	printk(KERN_INFO "n3t_device_add_iface(): ip netmask of %u\n", ipif->netmask);
	printk(KERN_INFO "n3t_device_add_iface(): ip broadcast address of %u\n", ipif->broadcast);
	printk(KERN_INFO "n3t_device_add_iface(): resgistering %s\n", next_dev->name);
	printk(KERN_INFO "n3t_device_add_iface(): hw address(pM) of %pM\n", next_dev->dev_addr);
	
	// dev_nameによるdevリストの探索
	// https://github.com/ryofslife/sample_network_stack/blob/main/net.c#L148
	for (entry = next_dev; entry; entry = next_dev->next) {
		if (entry->name == dev_name) {
			// 該当する仮想デバイスを渡されたいぴｆに追加する
			ipif->dev = entry;
			// 渡されたいぴｆをいぴｆリストの先頭に追加する
			ipif->next = next_ipif;
			next_ipif = ipif;
			return 0;
		}
	}
	//　該当する仮想デバイスはなし
	return -1;
}

// 返り値voidのいぴいｆをダンプする関数、デバイスドライバからn3t_device_add_ip_iface()の後に呼び出す
void dump_ip_ifaces(void)
{
	struct ip_iface *entry;
	
	// いｐいｆリストにあるインタフェイスを全てダンプする
	for (entry = next_ipif; entry; entry = next_ipif->next) {
		if (entry) {
			printk(KERN_INFO "dump_ip_ifaces(): ip unicast address of %u\n", entry->unicast);
			printk(KERN_INFO "dump_ip_ifaces(): ip netmask of %u\n", entry->netmask);
			printk(KERN_INFO "dump_ip_ifaces(): ip broadcast address of %u\n", entry->broadcast);
			printk(KERN_INFO "dump_ip_ifaces(): resgistering %s\n", entry->dev->name);
			printk(KERN_INFO "dump_ip_ifaces(): hw address(pM) of %pM\n", entry->dev->dev_addr);
		}
	}
}