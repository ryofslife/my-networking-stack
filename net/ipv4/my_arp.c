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
#include <net/my_net.h>



// arpリクエストに対するsanity check
static int my_arphdr_check(const struct arphdr *arp)
{
	// とりあえず全てパスしたことにしておく
	// 後で戻ってくる
	return 0;
}

static int my_arp_reply(int type, int ptype, struct net_device *dev, __be32 src_ip, __be32 dest_ip,
							const unsigned char *src_hw,
							const unsigned char *dest_hw, 
							const unsigned char *target_hw)
{
	struct sk_buff *skb;
	
	// 自分宛のためarp応答をする、まずはskbを用意する
	skb = my_arp_create(ARPOP_REPLY, ETH_P_ARP, dev, src_ip, dest_ip, src_hw, dest_hw, target_hw);
	
	// 用意したskbを送り出す
	return my_arp_send(skb);
			
}

static struct sk_buff *my_arp_create(int type, int ptype, struct net_device *dev, __be32 src_ip, __be32 dest_ip,
							const unsigned char *src_hw,
							const unsigned char *dest_hw, 
							const unsigned char *target_hw)
{
	// skbを用意する
	struct sk_buff *skb;
	// arpのデータ部へのポインタ、sip/tipなんかを投入する
	struct arphdr *arp;
	// *arp内でずらしながらsip/tipを投入していくためのポインタ
	unsigned char *arp_ptr;
	int hlen = LL_RESERVED_SPACE(dev);
	int tlen = dev->needed_tailroom;
	
	// skbを確保する
	skb = alloc_skb(arp_hdr_len(dev) + hlen + tlen, GFP_ATOMIC);
	
	// skbの確保に失敗
	if (!skb)
	{
		return NULL;
	}
	
	// skbのヘッダ分を確保する
	skb_reserve(skb, hlen);
	
	// これが何をしているのかわからない
	skb_reset_network_header(skb);
	
	// arpのデータ部を確保する、確保した*arpに対して色々投入していく
	arp = skb_put(skb, arp_hdr_len(dev));
	
	// デバイス情報を投入する、これに関しては要求で上がってきたskbからdevを抽出したので良いはず？
	skb->dev = dev;
	
	// プロトコルを指定する、ここで指定したプロトコルはどのタイミングで参照されるのか？
	skb->protocol = htons(ETH_P_ARP);
	
	// source hwアドレスが空の際に埋めとく、本来は埋めてからこの関数に渡す
	if (!src_hw)
	{
		src_hw = dev->dev_addr;
	}
	
	// dest hwアドレスが空の際に埋めとく、本来は埋めてからこの関数に渡す
	// 埋めようにも埋められないので、とりあえずbroadcastを投入しておく
	if (!dest_hw)
	{
		dest_hw = dev->broadcast;
	}
	
	// ethernet headerを埋める、丁寧に関数が用意されている、助かる
	if (dev_hard_header(skb, dev, ptype, dest_hw, src_hw, skb->len) < 0);
	{ 
		goto exit; 
	}
	
	
	// ここからarpのデータ部を投入してく
	// hwタイプを投入する、ehter？
	arp->ar_hrd = htons(dev->type);
	// プロトコルタイプを投入する、IPとなる
	arp->ar_pro = htons(ETH_P_IP);
	// ハードウェアアドレス長を投入する
	arp->ar_hln = dev->addr_len;
	// プロトコルアドレス長を投入する、IPなので32bits(４バイト)
	arp->ar_pln = 4;
	// オペコードを投入する、リプライなので２、リクエストの場合は１
	arp->ar_op = htons(type);
	
	// メインディッシュを投入してく
	// 前菜が全部で1バイトなので*arpを１バイト分ずらす
	arp_ptr = (unsigned char *)(arp + 1);
	// いきなり自分のMACを投入！
	memcpy(arp_ptr, src_hw, dev->addr_len);
	// *arpをetherアドレス分だけずらす
	arp_ptr += dev->addr_len;
	//　自分のIPを投入する
	memcpy(arp_ptr, &src_ip, 4);
	// IPは４バイト、投入した分だけずらす
	arp_ptr += 4;
	// リクエスト元のetherアドレスを投入する、linuxでは異常系の処理を行っているがここではしない
	memcpy(arp_ptr, target_hw, dev->addr_len);
	//　etherアドレス分だけずらす
	arp_ptr += dev->addr_len;
	// リクエスト元のIPを投入する
	memcpy(arp_ptr, &dest_ip, 4);
	
	return skb;
	
exit:
	// ちゃんとskbは解放してあげる
	kfree_skb(skb);
	// とりあえずNULLを返して置けば良い？
	return NULL;
	
}

static int my_arp_send(struct sk_buff *skb)
{
	// これが何をしているのかいまいち把握できていない、とりあえずコメントアウトする
	// skb_dst_set(skb, dst_clone(dst));
	// skbを送信する
	return dev_queue_xmit(skb);
	
}

// arpの受信ハンドラ
// このハンドラはヘッダファイルに含めなくて良い？、ソースコードを読む限りpacket_type.funcに渡すだけ
static int my_arp_rcv(struct sk_buff *skb, struct net_device *dev,
		   struct packet_type *pt, struct net_device *orig_dev)
{
	
	int drop_reason;
	const struct arphdr *arp;
	unsigned char *arp_ptr;
	unsigned char *sha;
	unsigned char *tha;
	__be32 sip, tip;
	
	// arp応答に構築に必要な材料を定義する
	struct net_device *dev = skb->dev;
	
	arp = arp_hdr(skb);
	
	// ポインタをshaにずらす
	arp_ptr = (unsigned char *)(arp + 1);
	sha	= arp_ptr;
	
	//　ポインタをsipにずらす
	arp_ptr += dev->addr_len;
	memcpy(&sip, arp_ptr, 4);

	// ポインタをthaにずらす
	arp_ptr += 4;
	tha = arp_ptr;
	
	// ポインタをtipにずらす
	arp_ptr += dev->addr_len;
	memcpy(&tip, arp_ptr, 4);

	/* とりあえずここまでパスした場合はダンプしておく */
	printk(KERN_INFO "my_arp_rcv(): address of skb　%p\n", skb);
	printk(KERN_INFO "my_arp_rcv(): address of arp header%p\n", arp);
	// printk(KERN_INFO "my_arp_rcv(): sender hardware address of %s\n", sha);
	printk(KERN_INFO "my_arp_rcv(): sender IP address of %u\n", sip);
	// printk(KERN_INFO "my_arp_rcv(): target hardware address %s\n", tha);
	printk(KERN_INFO "my_arp_rcv(): target IP address of %u\n", tip);	
	
	// 届いたarpのsanity check
	if (my_arphdr_check(arp) == 0)
	{
		printk("my_arp_rcv(): the arp requsest is for IP protocol\n");
	} else {
		printk("my_arp_rcv(): the arp requsest was for a protocol other than IP\n");
		goto exit;
	}
	
	// IPインタフェイスを探索、該当するIPアドレスがあるかどうか確認
	if (find_ip_iface(tip) == 1)
	{
		printk("my_arp_rcv(): found matching ip interface\n");
		
		// arp応答する
		if (my_arp_reply(ARPOP_REPLY, ETH_P_ARP, dev, sip, tip, dev->dev_addr,　sha, sha) == 0)
		{
			printk("my_arp_rcv(): successfully sent an arp response\n");
		} else {
			printk("my_arp_rcv(): failed sending an arp response\n");
		}
		
	} else {
		printk("my_arp_rcv(): no matching ip interface found\n");
	}
	
	drop_reason = SKB_DROP_REASON_NOT_SPECIFIED;
	kfree_skb_reason(skb, drop_reason);

	return 0;
	
exit:
	// ちゃんとskbは解放してあげる
	kfree_skb(skb);
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
