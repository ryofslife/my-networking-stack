#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/capability.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/mm.h>
#include <linux/inet.h>
#include <linux/inetdevice.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/fddidevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/net.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif

#include <net/net_namespace.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <net/route.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <net/sock.h>
#include <net/arp.h>
#include <net/ax25.h>
#include <net/netrom.h>
#include <net/dst_metadata.h>
#include <net/ip_tunnels.h>

#include <linux/uaccess.h>

#include <linux/netfilter_arp.h>

#include <net/my_arp.h>



// arpリクエストに対するsanity check
// static int my_arphdr_check(struct arphdr *arp)
// {
	
// }


// arpの受信ハンドラ
// このハンドラはヘッダファイルに含めなくて良い？、ソースコードえお読む限りpacket_type.funcに渡すだけ
static int my_arp_rcv(struct sk_buff *skb, struct net_device *dev,
		   struct packet_type *pt, struct net_device *orig_dev)
{
	
	int drop_reason;
	const struct arphdr *arp;
	unsigned char *arp_ptr;
	// unsigned char *sha;
	// unsigned char *tha;
	__be32 sip, tip;
	
	arp = arp_hdr(skb);
	
	// ポインタをshaにずらす
	arp_ptr = (unsigned char *)(arp + 1);
	// sha	= arp_ptr;
	
	//　ポインタをsipにずらす
	arp_ptr += dev->addr_len;
	memcpy(&sip, arp_ptr, 4);

	// ポインタをthaにずらす
	arp_ptr += 4;
	// tha = arp_ptr;
	
	// ポインタをtipにずらす
	arp_ptr += dev->addr_len;
	memcpy(&tip, arp_ptr, 4);

	/* とりあえずここまでパスした場合はダンプしておく */
	printk(KERN_INFO "my_arp_rcv(): address of skb　%p\n", skb);
	printk(KERN_INFO "my_arp_rcv(): address of arp header%p\n", arp);
	// printk(KERN_INFO "my_arp_rcv(): sender hardware address of %s\n", sha);
	printk(KERN_INFO "my_arp_rcv(): sender IP address of %p\n", sip);
	// printk(KERN_INFO "my_arp_rcv(): target hardware address %s\n", tha);
	printk(KERN_INFO "my_arp_rcv(): target IP address of %p\n", tip);	
	
	// // 届いたarpのsanity check
	// if (my_arphdr_check(arp) == 0)
	// {
		// printk("my_arp_rcv(): the arp requsest is for IP protocol\n");
		// // IPインタフェイスを探索、該当するIPアドレスがあるかどうか確認
		// dump_ip_ifaces();
		// drop_reason = SKB_DROP_REASON_NOT_SPECIFIED;
		// kfree_skb_reason(skb, drop_reason);
	// } else {
		// printk("my_arp_rcv(): the arp requsest was for a protocol other than IP\n");
		// // IPプロトコルではないためskbを破棄
		// // drop_reason = SKB_DROP_REASON_NOT_SPECIFIED;
		// // kfree_skb_reason(skb, drop_reason);
	// }
	
	drop_reason = SKB_DROP_REASON_NOT_SPECIFIED;
	kfree_skb_reason(skb, drop_reason);

	return 0;
	
}

// linuxに渡すarpスタックの受信ハンドラ
// arpはIPのようなリスト形式ででデータが上がってくることはないのか？
static struct packet_type arp_packet_type __read_mostly = {
	.type =	cpu_to_be16(ETH_P_ARP),
	.func =	my_arp_rcv,
};

// arpスタックの初期化
int __init my_arp_init(void)
{
	dev_add_pack(&arp_packet_type);
	
	return 0;
}
