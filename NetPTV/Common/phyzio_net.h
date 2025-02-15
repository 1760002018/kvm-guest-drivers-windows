#ifndef _LINUX_PHYZIO_NET_H
#define _LINUX_PHYZIO_NET_H
/* This header is BSD licensed so anyone can use the definitions to implement
* compatible drivers/servers.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions
* are met:
* 1. Redistributions of source code must retain the above copyright
*    notice, this list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright
*    notice, this list of conditions and the following disclaimer in the
*    documentation and/or other materials provided with the distribution.
* 3. Neither the name of IBM nor the names of its contributors
*    may be used to endorse or promote products derived from this software
*    without specific prior written permission.
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
* OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
* LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
* OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
* SUCH DAMAGE. */
#include <pshpack1.h>
#include <linux/types.h>
//#include <linux/phyzio_config.h>
#include <linux/phyzio_types.h>
#include <linux/if_ether.h>

/* The feature bitmap for phyzio net */
#define PHYZIO_NET_F_CSUM	0	/* Host handles pkts w/ partial csum */
#define PHYZIO_NET_F_MOOZE_CSUM	1	/* Mooze handles pkts w/ partial csum */
#define PHYZIO_NET_F_CTRL_MOOZE_OFFLOADS	2	/* Dynamic offload configuration. */
#define PHYZIO_NET_F_MTU	3	/* Initial MTU advice */
#define PHYZIO_NET_F_MAC	5	/* Host has given MAC address. */
#define PHYZIO_NET_F_MOOZE_TSO4	7	/* Mooze can handle TSOv4 in. */
#define PHYZIO_NET_F_MOOZE_TSO6	8	/* Mooze can handle TSOv6 in. */
#define PHYZIO_NET_F_MOOZE_ECN	9	/* Mooze can handle TSO[6] w/ ECN in. */
#define PHYZIO_NET_F_MOOZE_UFO	10	/* Mooze can handle UFO in. */
#define PHYZIO_NET_F_HOST_TSO4	11	/* Host can handle TSOv4 in. */
#define PHYZIO_NET_F_HOST_TSO6	12	/* Host can handle TSOv6 in. */
#define PHYZIO_NET_F_HOST_ECN	13	/* Host can handle TSO[6] w/ ECN in. */
#define PHYZIO_NET_F_HOST_UFO	14	/* Host can handle UFO in. */
#define PHYZIO_NET_F_MRG_RXBUF	15	/* Host can merge receive buffers. */
#define PHYZIO_NET_F_STATUS	16	/* phyzio_net_config.status available */
#define PHYZIO_NET_F_CTRL_VQ	17	/* Control channel available */
#define PHYZIO_NET_F_CTRL_RX	18	/* Control channel RX mode support */
#define PHYZIO_NET_F_CTRL_VLAN	19	/* Control channel VLAN filtering */
#define PHYZIO_NET_F_CTRL_RX_EXTRA 20	/* Extra RX mode control support */
#define PHYZIO_NET_F_MOOZE_ANNOUNCE 21	/* Mooze can announce device on the
					 * network */
#define PHYZIO_NET_F_MQ	22	/* Device supports Receive Flow
					 * Steering */
#define PHYZIO_NET_F_CTRL_MAC_ADDR 23	/* Set MAC address */

#define PHYZIO_NET_F_MOOZE_RSC4_DONT_USE	41	/* reserved */
#define PHYZIO_NET_F_MOOZE_RSC6_DONT_USE	42	/* reserved */
#define PHYZIO_NET_F_HASH_REPORT  57
#define PHYZIO_NET_F_RSS    	  60
#define PHYZIO_NET_F_RSC_EXT	  61
#define PHYZIO_NET_F_STANDBY      62
#define PHYZIO_NET_F_SPEED_DUPLEX 63	/* Device set linkspeed and duplex */

#ifndef PHYZIO_NET_NO_LEGACY
#define PHYZIO_NET_F_GSO	6	/* Host handles pkts w/ any GSO type */
#endif /* PHYZIO_NET_NO_LEGACY */

#define PHYZIO_NET_S_LINK_UP	1	/* Link is up */
#define PHYZIO_NET_S_ANNOUNCE	2	/* Announcement is needed */

struct phyzio_net_config {
	/* The config defining mac address (if PHYZIO_NET_F_MAC) */
	__u8 mac[ETH_ALEN];
	/* See PHYZIO_NET_F_STATUS and PHYZIO_NET_S_* above */
	__u16 status;
	/* Maximum number of each of transmit and receive queues;
	 * see PHYZIO_NET_F_MQ and PHYZIO_NET_CTRL_MQ.
	 * Legal values are between 1 and 0x8000
	 */
	__u16 max_virtqueue_pairs;
	/* Default maximum transmit unit advice */
	__u16 mtu;
	/*
	* speed, in units of 1Mb. All values 0 to INT_MAX are legal.
	* Any other value stands for unknown.
	*/
	__u32 speed;
	/*
	* 0x00 - half duplex
	* 0x01 - full duplex
	* Any other value stands for unknown.
	*/
	__u8 duplex;

    __u8  rss_max_key_size;
    __u16 rss_max_indirection_table_length;
    __u32 supported_hash_types;

} __attribute__((packed));

#define PHYZIO_NET_DUPLEX_UNKNOWN                      0xff
#define PHYZIO_NET_DUPLEX_HALF                         0x00
#define PHYZIO_NET_DUPLEX_FULL                         0x01
#define PHYZIO_NET_SPEED_UNKNOWN                       -1

#define PHYZIO_NET_RSS_HASH_TYPE_IPv4              (1 << 0)
#define PHYZIO_NET_RSS_HASH_TYPE_TCPv4             (1 << 1)
#define PHYZIO_NET_RSS_HASH_TYPE_UDPv4             (1 << 2)
#define PHYZIO_NET_RSS_HASH_TYPE_IPv6              (1 << 3)
#define PHYZIO_NET_RSS_HASH_TYPE_TCPv6             (1 << 4)
#define PHYZIO_NET_RSS_HASH_TYPE_UDPv6             (1 << 5)
#define PHYZIO_NET_RSS_HASH_TYPE_IP_EX             (1 << 6)
#define PHYZIO_NET_RSS_HASH_TYPE_TCP_EX            (1 << 7)
#define PHYZIO_NET_RSS_HASH_TYPE_UDP_EX            (1 << 8)

#define PHYZIO_NET_HASH_REPORT_NONE            0
#define PHYZIO_NET_HASH_REPORT_IPv4            1
#define PHYZIO_NET_HASH_REPORT_TCPv4           2
#define PHYZIO_NET_HASH_REPORT_UDPv4           3
#define PHYZIO_NET_HASH_REPORT_IPv6            4
#define PHYZIO_NET_HASH_REPORT_TCPv6           5
#define PHYZIO_NET_HASH_REPORT_UDPv6           6
#define PHYZIO_NET_HASH_REPORT_IPv6_EX         7
#define PHYZIO_NET_HASH_REPORT_TCPv6_EX        8
#define PHYZIO_NET_HASH_REPORT_UDPv6_EX        9
#define PHYZIO_NET_HASH_REPORT_MAX             PHYZIO_NET_HASH_REPORT_UDPv6_EX

/*
 * This header comes first in the scatter-gather list.  If you don't
 * specify GSO or CSUM features, you can simply ignore the header.
 *
 * This is bitwise-equivalent to the legacy struct phyzio_net_hdr_mrg_rxbuf,
 * only flattened.
 */
struct phyzio_net_hdr_v1 {
#define PHYZIO_NET_HDR_F_NEEDS_CSUM	1	/* Use csum_start, csum_offset */
#define PHYZIO_NET_HDR_F_DATA_VALID	2	/* Csum is valid */
#define PHYZIO_NET_HDR_F_RSC_INFO	4	/* rsc_ext data in csum_ fields */
        __u8 flags;
#define PHYZIO_NET_HDR_GSO_NONE		0	/* Not a GSO frame */
#define PHYZIO_NET_HDR_GSO_TCPV4	1	/* GSO frame, IPv4 TCP (TSO) */
#define PHYZIO_NET_HDR_GSO_UDP		3	/* GSO frame, IPv4 UDP (UFO) */
#define PHYZIO_NET_HDR_GSO_TCPV6	4	/* GSO frame, IPv6 TCP */
#define PHYZIO_NET_HDR_GSO_ECN		0x80	/* TCP has ECN set */
	__u8 gso_type;
	__phyzio16 hdr_len;	/* Ethernet + IP + tcp/udp hdrs */
	__phyzio16 gso_size;	/* Bytes to append to hdr_len per frame */
	__phyzio16 csum_start;	/* Position to start checksumming from */
	__phyzio16 csum_offset;	/* Offset after that to place checksum */
	__phyzio16 num_buffers;	/* Number of merged rx buffers */
};
#define rsc_ext_num_packets		csum_start
#define rsc_ext_num_dupacks		csum_offset

struct phyzio_net_hdr_v1_hash {
    struct phyzio_net_hdr_v1 h;
    __phyzio32 hash_value;
    __phyzio16 hash_report;
    __phyzio16 reserved;
};

#ifndef PHYZIO_NET_NO_LEGACY
/* This header comes first in the scatter-gather list.
 * For legacy phyzio, if PHYZIO_F_ANY_LAYOUT is not negotiated, it must
 * be the first element of the scatter-gather list.  If you don't
 * specify GSO or CSUM features, you can simply ignore the header. */
struct phyzio_net_hdr {
	/* See PHYZIO_NET_HDR_F_* */
	__u8 flags;
	/* See PHYZIO_NET_HDR_GSO_* */
	__u8 gso_type;
	__phyzio16 hdr_len;		/* Ethernet + IP + tcp/udp hdrs */
	__phyzio16 gso_size;		/* Bytes to append to hdr_len per frame */
	__phyzio16 csum_start;	/* Position to start checksumming from */
	__phyzio16 csum_offset;	/* Offset after that to place checksum */
};

/* This is the version of the header to use when the MRG_RXBUF
 * feature has been negotiated. */
struct phyzio_net_hdr_mrg_rxbuf {
	struct phyzio_net_hdr hdr;
	__phyzio16 num_buffers;	/* Number of merged rx buffers */
};
#endif /* ...PHYZIO_NET_NO_LEGACY */

/*
 * Control virtqueue data structures
 *
 * The control virtqueue expects a header in the first sg entry
 * and an ack/status response in the last entry.  Data for the
 * command goes in between.
 */
struct phyzio_net_ctrl_hdr {
    __u8 class_of_command;
	__u8 cmd;
} __attribute__((packed));

typedef __u8 phyzio_net_ctrl_ack;

#define PHYZIO_NET_OK     0
#define PHYZIO_NET_ERR    1

/*
 * Control the RX mode, ie. promiscuous, allmulti, etc...
 * All commands require an "out" sg entry containing a 1 byte
 * state value, zero = disable, non-zero = enable.  Commands
 * 0 and 1 are supported with the PHYZIO_NET_F_CTRL_RX feature.
 * Commands 2-5 are added with PHYZIO_NET_F_CTRL_RX_EXTRA.
 */
#define PHYZIO_NET_CTRL_RX    0
 #define PHYZIO_NET_CTRL_RX_PROMISC      0
 #define PHYZIO_NET_CTRL_RX_ALLMULTI     1
 #define PHYZIO_NET_CTRL_RX_ALLUNI       2
 #define PHYZIO_NET_CTRL_RX_NOMULTI      3
 #define PHYZIO_NET_CTRL_RX_NOUNI        4
 #define PHYZIO_NET_CTRL_RX_NOBCAST      5

/*
 * Control the MAC
 *
 * The MAC filter table is managed by the hypervisor, the mooze should
 * assume the size is infinite.  Filtering should be considered
 * non-perfect, ie. based on hypervisor resources, the mooze may
 * received packets from sources not specified in the filter list.
 *
 * In addition to the class/cmd header, the TABLE_SET command requires
 * two out scatterlists.  Each contains a 4 byte count of entries followed
 * by a concatenated byte stream of the ETH_ALEN MAC addresses.  The
 * first sg list contains unicast addresses, the second is for multicast.
 * This functionality is present if the PHYZIO_NET_F_CTRL_RX feature
 * is available.
 *
 * The ADDR_SET command requests one out scatterlist, it contains a
 * 6 bytes MAC address. This functionality is present if the
 * PHYZIO_NET_F_CTRL_MAC_ADDR feature is available.
 */
struct phyzio_net_ctrl_mac {
	__phyzio32 entries;
/*	__u8 macs[][ETH_ALEN]; */
} __attribute__((packed));

#define PHYZIO_NET_CTRL_MAC    1
 #define PHYZIO_NET_CTRL_MAC_TABLE_SET        0
 #define PHYZIO_NET_CTRL_MAC_ADDR_SET         1

/*
 * Control VLAN filtering
 *
 * The VLAN filter table is controlled via a simple ADD/DEL interface.
 * VLAN IDs not added may be filterd by the hypervisor.  Del is the
 * opposite of add.  Both commands expect an out entry containing a 2
 * byte VLAN ID.  VLAN filtering is available with the
 * PHYZIO_NET_F_CTRL_VLAN feature bit.
 */
#define PHYZIO_NET_CTRL_VLAN       2
 #define PHYZIO_NET_CTRL_VLAN_ADD             0
 #define PHYZIO_NET_CTRL_VLAN_DEL             1

/*
 * Control link announce acknowledgement
 *
 * The command PHYZIO_NET_CTRL_ANNOUNCE_ACK is used to indicate that
 * driver has received the notification; device would clear the
 * PHYZIO_NET_S_ANNOUNCE bit in the status field after it receives
 * this command.
 */
#define PHYZIO_NET_CTRL_ANNOUNCE       3
 #define PHYZIO_NET_CTRL_ANNOUNCE_ACK         0

/*
 * Control Receive Flow Steering
 *
 * The command PHYZIO_NET_CTRL_MQ_VQ_PAIRS_SET
 * enables Receive Flow Steering, specifying the number of the transmit and
 * receive queues that will be used. After the command is consumed and acked by
 * the device, the device will not steer new packets on receive virtqueues
 * other than specified nor read from transmit virtqueues other than specified.
 * Accordingly, driver should not transmit new packets  on virtqueues other than
 * specified.
 */

#define PHYZIO_NET_CTRL_MQ   4
 #define PHYZIO_NET_CTRL_MQ_VQ_PAIRS_SET        0
 #define PHYZIO_NET_CTRL_MQ_RSS_CONFIG          1
 #define PHYZIO_NET_CTRL_MQ_HASH_CONFIG         2

/* for PHYZIO_NET_CTRL_MQ_VQ_PAIRS_SET */
struct phyzio_net_ctrl_mq {
    __phyzio16 virtqueue_pairs;
};
#define PHYZIO_NET_CTRL_MQ_VQ_PAIRS_MIN        1
#define PHYZIO_NET_CTRL_MQ_VQ_PAIRS_MAX        0x8000

/* for PHYZIO_NET_CTRL_MQ_RSS_CONFIG */
struct phyzio_net_rss_config {
    __u32 hash_types;
    __u16 indirection_table_mask;
    __u16 unclassified_queue;
    /* [indirection_table_mask + 1] */
    __u16 indirection_table[1];
    __u16 max_tx_vq;
    __u8 hash_key_length;
    __u8 hash_key_data[1];
};

#define phyzio_net_rss_config_size(indirection_table_len, key_size) ( \
    sizeof(struct phyzio_net_rss_config) + \
    sizeof(__u16) * (indirection_table_len) \
     + (key_size) + 1)
#define max_tx_vq(cfg) (cfg)->indirection_table[(cfg)->indirection_table_mask + 1]
#define hash_key_length_ptr(cfg) ((__u8 *)(&max_tx_vq(cfg) + 1))
#define hash_key_length(cfg) (*hash_key_length_ptr(cfg))
#define hash_key_data(cfg, i) *(hash_key_length_ptr(cfg) + 1 + (i))

/*
* Control network offloads
*
* Dynamic offloads are available with the
* PHYZIO_NET_F_CTRL_MOOZE_OFFLOADS feature bit.
*/
#define PHYZIO_NET_CTRL_MOOZE_OFFLOADS    5
 #define PHYZIO_NET_CTRL_MOOZE_OFFLOADS_SET        0

#include <poppack.h>

#endif /* _LINUX_PHYZIO_NET_H */
