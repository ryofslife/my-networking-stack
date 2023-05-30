// #ifndef _LINUX_SKBUFF_H
// #define _LINUX_SKBUFF_H

// #include <linux/kernel.h>
// #include <linux/compiler.h>
// #include <linux/time.h>
// #include <linux/bug.h>
// #include <linux/bvec.h>
// #include <linux/cache.h>
// #include <linux/rbtree.h>
// #include <linux/socket.h>
// #include <linux/refcount.h>

// #include <linux/atomic.h>
// #include <asm/types.h>
// #include <linux/spinlock.h>
// #include <linux/net.h>
// #include <linux/textsearch.h>
// #include <net/checksum.h>
// #include <linux/rcupdate.h>
// #include <linux/hrtimer.h>
// #include <linux/dma-mapping.h>
// #include <linux/netdev_features.h>
// #include <linux/sched.h>
// #include <linux/sched/clock.h>
// #include <net/flow_dissector.h>
// #include <linux/splice.h>
// #include <linux/in6.h>
// #include <linux/if_packet.h>
// #include <linux/llist.h>
// #include <net/flow.h>
// #include <net/page_pool.h>
// #if IS_ENABLED(CONFIG_NF_CONNTRACK)
// #include <linux/netfilter/nf_conntrack_common.h>
// #endif
// #include <net/net_debug.h>
// #include <net/dropreason.h>

void dump_ip_ifaces(void);
int find_ip_iface(unsigned int tip);