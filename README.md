# Custom network stack for Raspberry Pi.

I'm working on a built-in network stack for the Linux kernel. 
It's purely for educational purpose😏.

```
┌──(kali㉿kali-raspberry-pi)-[~]
└─$ dmesg
...
[   57.142243] packet from ip_list_rcv(): 0xffffff80059b4800
[   57.142308] Source Address: 020aa8c0
[   57.142335] Destination Address   : ff0aa8c0
[   87.489575] packet from ip_list_rcv(): 0xffffff80059b4c00
[   87.489646] Source Address: 020aa8c0
[   87.489674] Destination Address   : ff0aa8c0
...
```
