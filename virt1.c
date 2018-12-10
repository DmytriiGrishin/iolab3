#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/moduleparam.h>
#include <net/arp.h>

#define ERR(...) printk( KERN_ERR ""__VA_ARGS__ )
#define LOG(...) printk( KERN_INFO ""__VA_ARGS__ )

static char* link = "eth0";
module_param( link, charp, 0 );

static char* ifname = "virt"; 
module_param( ifname, charp, 0 );

static struct net_device *child = NULL;

static struct net_device_stats stats;

struct priv {
   struct net_device *parent;
};

static rx_handler_result_t handle_frame( struct sk_buff **pskb ) {
   struct sk_buff *skb = *pskb;
   if (skb->protocol == htons(ETH_P_ARP)) {
      LOG("AND PROTOCOL IS: %d\n", skb->protocol);
      struct arphdr* arph = arp_hdr(skb);
      if (arph->ar_op == htons(ARPOP_REPLY)) {
         unsigned char* arpptr = skb_network_header(*pskb) + sizeof(*arph);
         LOG("ARP reply sender MAC: %2x:%2x:%2x:%2x:%2x:%2x\n", arpptr[5], arpptr[4], arpptr[3], arpptr[2], arpptr[1], arpptr[0]);
         LOG("ARP reply sender IP: %d.%d.%d.%d\n", arpptr[6], arpptr[7], arpptr[8], arpptr[9]);
         arpptr += 10;
         LOG("ARP reply needed MAC: %2x:%2x:%2x:%2x:%2x:%2x\n", arpptr[0], arpptr[1], arpptr[2], arpptr[3], arpptr[4], arpptr[5]);
         LOG("ARP reply needed IP: %d.%d.%d.%d\n", arpptr[6], arpptr[7], arpptr[8], arpptr[9]);
      }
      if (arph->ar_op == htons(ARPOP_REQUEST)) {
         unsigned char* arpptr = skb_network_header(*pskb) + sizeof(*arph);
         LOG("ARP request sender MAC: %2x:%2x:%2x:%2x:%2x:%2x\n", arpptr[5], arpptr[4], arpptr[3], arpptr[2], arpptr[1], arpptr[0]);
         LOG("ARP request sender IP: %d.%d.%d.%d\n", arpptr[6], arpptr[7], arpptr[8], arpptr[9]);
         arpptr += 10;
         LOG("ARP request needed MAC: %2x:%2x:%2x:%2x:%2x:%2x\n", arpptr[0], arpptr[1], arpptr[2], arpptr[3], arpptr[4], arpptr[5]);
         LOG("ARP request needed IP: %d.%d.%d.%d\n", arpptr[6], arpptr[7], arpptr[8], arpptr[9]);
      }
      stats.rx_packets++;
      stats.rx_bytes += skb->len;
   }
   return RX_HANDLER_PASS;
}

static int open( struct net_device *dev ) {
   netif_start_queue( dev );
   LOG( "%s: device opened\n", dev->name );
   return 0;
}

static int stop( struct net_device *dev ) {
   netif_stop_queue( dev );
   LOG( "%s: device closed\n", dev->name );
   return 0;
}

static netdev_tx_t start_xmit( struct sk_buff *skb, struct net_device *dev ) {
   struct priv *priv = netdev_priv( dev );
   stats.tx_packets++;
   stats.tx_bytes += skb->len;
   if( priv->parent ) {
      skb->dev = priv->parent;
      skb->priority = 1;
      dev_queue_xmit( skb );
      // LOG( "tx: injecting frame from %s to %s", dev->name, skb->dev->name );
      return 0;
   }
   return NETDEV_TX_OK;
}

static struct net_device_stats *get_stats( struct net_device *dev ) {
   return &stats;
}

static struct net_device_ops crypto_net_device_ops = {
   .ndo_open = open,
   .ndo_stop = stop,
   .ndo_get_stats = get_stats,
   .ndo_start_xmit = start_xmit,
};

// #define MAX_ADDR_LEN    32  <netdev.h>
// #define ETH_ALEN        6   /* Octets in one ethernet addr   */ <if_ether.h>

static void setup( struct net_device *dev ) {
   int j;
   ether_setup( dev );
   memset( netdev_priv(dev), 0, sizeof( struct priv ) );
   dev->netdev_ops = &crypto_net_device_ops;
   for( j = 0; j < ETH_ALEN; ++j ) // fill in the MAC address with a phoney 
      dev->dev_addr[ j ] = (char)j;
}

int __init init( void ) {
   int err = 0;
   struct priv *priv;
   char ifstr[ 40 ];
   sprintf( ifstr, "%s%s", ifname, "%d" );
   child = alloc_netdev( sizeof( struct priv ), ifstr, 'd', setup );
   if( child == NULL ) {
      ERR( "%s: allocate error", THIS_MODULE->name ); return -ENOMEM;
   }
   priv = netdev_priv( child );
   priv->parent = __dev_get_by_name( &init_net, link ); // parent interface  
   if( !priv->parent ) {
      ERR( "%s: no such net: %s", THIS_MODULE->name, link );
      err = -ENODEV; goto err;
   }
   if( priv->parent->type != ARPHRD_ETHER && priv->parent->type != ARPHRD_LOOPBACK ) {
      ERR( "%s: illegal net type", THIS_MODULE->name );
      err = -EINVAL; goto err;
   }
   /* also, and clone its IP, MAC and other information */
   memcpy( child->dev_addr, priv->parent->dev_addr, ETH_ALEN );
   memcpy( child->broadcast, priv->parent->broadcast, ETH_ALEN );
   if( ( err = dev_alloc_name( child, child->name ) ) ) {
      ERR( "%s: allocate name, error %i", THIS_MODULE->name, err );
      err = -EIO; goto err;
   }
   register_netdev( child );
   rtnl_lock();
   netdev_rx_handler_register( priv->parent, &handle_frame, NULL );
   rtnl_unlock();
   LOG( "module %s loaded", THIS_MODULE->name );
   LOG( "%s: create link %s", THIS_MODULE->name, child->name );
   LOG( "%s: registered rx handler for %s", THIS_MODULE->name, priv->parent->name );
   return 0;
err:
   free_netdev( child );
   return err;
}

void __exit exit( void ) {
   struct priv *priv = netdev_priv( child );
   if( priv->parent ) {
      rtnl_lock();
      netdev_rx_handler_unregister( priv->parent );
      rtnl_unlock();
      LOG( "unregister rx handler for %s\n", priv->parent->name );
   }
   unregister_netdev( child );
   free_netdev( child );
   LOG( "module %s unloaded", THIS_MODULE->name );
}

module_init(init);
module_exit(exit);

MODULE_AUTHOR("Ifmo university");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("2.1b");