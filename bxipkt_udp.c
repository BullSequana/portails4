/*
 * Copyright (c) 2019-2019 Bull S.A.S
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *    - Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    - Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <assert.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <net/ethernet.h>
#include <sys/ioctl.h>
#include <net/if.h>

#include "bxipkt.h"
#include "utils.h"
#include "ptl_log.h"

/*
 * We subtract IP, UDP, and bximsg header sizes from interface MTU
 * to get the maximum bytes we can transmit in a single packet.
 * (IP max header size is the base size added with the max length of options)
 */
#define IP_MAX_HDR_SIZE (sizeof(struct iphdr) + MAX_IPOPTLEN)

#define BXIPKT_UDP_PORT_MIN 8000

#define BXIPKT_MAGIC_NUMBER ((uint32_t)0x82D6A19F)

#define BXIPKT_UDP_HDR_SIZE (sizeof(BXIPKT_MAGIC_NUMBER) + sizeof(struct bximsg_hdr))

#ifdef DEBUG
/*
 * log to stderr, with the give debug level
 */
#define LOGN(n, ...)                                                                               \
	do {                                                                                       \
		if (bxipkt_debug >= (n))                                                           \
			ptl_log(__VA_ARGS__);                                                      \
	} while (0)

#define LOG(...) LOGN(1, __VA_ARGS__)
#else
#define LOGN(n, ...)                                                                               \
	do {                                                                                       \
	} while (0)
#define LOG(...)                                                                                   \
	do {                                                                                       \
	} while (0)
#endif

#include "ptl_getenv.h"

struct bxipkt_buflist {
	struct bxipkt_buf *freelist, *pool_data;
	size_t size;
	unsigned int count;
};

struct bxipkt_iface {
	void *arg;
	void (*input)(void *arg, void *data, size_t size, struct bximsg_hdr *hdr, int nid, int pid,
		      int uid);
	void (*output)(void *arg, struct bxipkt_buf *pkt);
	void (*sent_pkt)(struct bxipkt_buf *pkt);
	unsigned long ipkts;
	unsigned long opkts;
	unsigned long apkts;
	unsigned long iipkts;
	unsigned long iopkts;
	struct bxipkt_buflist tx_buflist;

	unsigned char *rx_buf;
	size_t rx_bufsize;

	uint32_t net_addr;
	uint32_t net_mask;
	int nid;

	/*
	 * The portals pid is based on the port of the socket and follow
	 * this rule: <socket port> = <portals pid> + 1024
	 */
	int pid;
	int sockfd;
};

static uint32_t bxipkt_net;
static int bxipkt_mtu;

/* Library initialization. */
int bxipktudp_libinit(struct bxipkt_options *o)
{
	int ret = 0;
	struct in_addr addr;
	struct bxipkt_udp_options *opts = (struct bxipkt_udp_options *)o;

	if (opts->default_mtu)
		bxipkt_mtu = ETHERMTU;

	if (inet_aton(opts->ip, &addr) != 0) {
		bxipkt_net = ntohl(addr.s_addr);
	} else {
		LOGN(0, "%s: invalid network address: %s\n", __func__, opts->ip);
		ret = PTL_FAIL;
	}

	return ret;
}

/* Library finalization. */
void bxipktudp_libfini(void)
{
}

static void dump_sockaddr_in(const char *str, struct sockaddr_in *in)
{
	LOGN(4, "%s: family=%d, %s:%d\n", str, in->sin_family, inet_ntoa(in->sin_addr),
	     ntohs(in->sin_port));
}

static size_t bxipktudp_getmtu(const char *itfname)
{
	int sockfd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, IPPROTO_IP);
	struct ifreq ifr;

	if (sockfd == -1)
		ptl_panic("%s: socket error: %s", __func__, strerror(errno));

	(void)strcpy(ifr.ifr_name, itfname);

	if (ioctl(sockfd, SIOCGIFMTU, (caddr_t)&ifr))
		ptl_panic("%s: socket ioctl error: %s", __func__, strerror(errno));

	close(sockfd);

	LOGN(1, "%s: interface %s mtu %u\n", __func__, itfname, ifr.ifr_mtu);

	return ifr.ifr_mtu;
}

int bxipktudp_netconfig(struct bxipkt_iface *iface)
{
	struct ifaddrs *addrs;
	struct ifaddrs *tmp;
	struct sockaddr_in *ifa_addr;
	struct sockaddr_in *ifa_netmask;
	size_t mtu = bxipkt_mtu;
	size_t max_hdr_size;

	if (getifaddrs(&addrs) != 0) {
		LOGN(0, "Failed to retrieve the list of interfaces: %s\n", strerror(errno));
		return 0;
	}

	for (tmp = addrs; tmp != NULL; tmp = tmp->ifa_next) {
		if (tmp->ifa_addr == NULL || tmp->ifa_addr->sa_family != AF_INET)
			continue;

		ifa_addr = (struct sockaddr_in *)tmp->ifa_addr;
		iface->net_addr = ntohl(ifa_addr->sin_addr.s_addr);

		ifa_netmask = (struct sockaddr_in *)tmp->ifa_netmask;
		iface->net_mask = ntohl(ifa_netmask->sin_addr.s_addr);

		if (((iface->net_addr ^ bxipkt_net) & iface->net_mask) == 0) {
			dump_sockaddr_in(__func__, ifa_addr);

			if (!mtu)
				mtu = bxipktudp_getmtu(tmp->ifa_name);

			break;
		}
	}

	if (tmp == NULL) {
		LOGN(0, "Failed to find the interface for network %s\n", getenv("PORTALS4_NET"));
		freeifaddrs(addrs);
		return 0;
	}

	iface->nid = iface->net_addr & ~iface->net_mask;

	max_hdr_size = IP_MAX_HDR_SIZE + sizeof(struct udphdr) + BXIPKT_UDP_HDR_SIZE;
	if (mtu <= max_hdr_size) {
		LOGN(0, "The mtu (%lu) is less or equal than %lu\n", mtu, max_hdr_size);
		freeifaddrs(addrs);
		return 0;
	}

	iface->tx_buflist.size = mtu - max_hdr_size;

	iface->rx_bufsize = BXIPKT_UDP_HDR_SIZE + iface->tx_buflist.size;

	LOGN(2, "%s: using %s: nid=%u, tx size=%lu, rx size=%lu, net addr=0x%x, net mask=0x%x\n",
	     __func__, tmp->ifa_name, iface->nid, iface->tx_buflist.size, iface->rx_bufsize,
	     iface->net_addr, iface->net_mask);

	freeifaddrs(addrs);

	return 1;
}

int bxipktudp_createsocket(struct bxipkt_iface *iface, int port, char *err_msg)
{
	int sock;
	struct sockaddr_in server_address;

	memset(&server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(port);
	server_address.sin_addr.s_addr = htonl(iface->net_addr);

	sock = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, IPPROTO_IP);
	if (sock < 0) {
		snprintf(err_msg, PTL_LOG_BUF_SIZE, "socket call error: %s", strerror(errno));
		return sock;
	}

	dump_sockaddr_in(__func__, &server_address);

	if ((bind(sock, (struct sockaddr *)&server_address, sizeof(server_address))) < 0) {
		snprintf(err_msg, PTL_LOG_BUF_SIZE, "bind call error: %s", strerror(errno));
		close(sock);
		return -1;
	}

	LOGN(3, "%s: sock=%d, port=%d\n", __func__, sock, port);

	return sock;
}

int bxipktudp_buflist_init(struct bxipkt_buflist *l)
{
	int i;
	struct bxipkt_buf *b;
	char *p;

	if (l->count == 0) {
		LOG("malloc(%s): invalid count value\n", __func__);
		return 0;
	}

	l->pool_data = calloc(l->count, sizeof(struct bxipkt_buf));
	if (l->pool_data == NULL) {
		LOG("malloc(%s): %s\n", __func__, strerror(errno));
		return 0;
	}

	b = l->pool_data;
	for (i = 0; i < l->count; i++) {
		p = calloc(1, BXIPKT_UDP_HDR_SIZE + l->size);
		if (p == NULL) {
			LOG("malloc(%s): %s\n", __func__, strerror(errno));
			return 0;
		}
		p += BXIPKT_UDP_HDR_SIZE;
		b->addr = p;

		b->next = i < l->count - 1 ? b + 1 : NULL;
		b->index = i;
		b++;
	}

	l->freelist = l->pool_data;
	return 1;
}

void bxipktudp_buflist_done(struct bxipkt_buflist *l)
{
	struct bxipkt_buf *b;
	int i;
	char *p;

	b = l->pool_data;

	if (b == NULL)
		return;

	for (i = 0; i < l->count; i++) {
		p = b->addr;
		if (p)
			free(p - BXIPKT_UDP_HDR_SIZE);
		b++;
	}

	free(l->pool_data);
	l->pool_data = NULL;
}

int bxipktudp_common_send(struct bxipkt_iface *iface, char *buf, size_t buf_len,
			  struct bximsg_hdr *hdr_data, int nid, int pid)
{
	struct sockaddr_in si_other;
	int len;
	uint32_t remote_addr, tmp;
	int log_level;

	remote_addr = (iface->net_addr & iface->net_mask) | nid;

	memset(&si_other, 0, sizeof(si_other));
	si_other.sin_family = AF_INET;
	si_other.sin_addr.s_addr = htonl(remote_addr);
	si_other.sin_port = htons(pid + BXIPKT_UDP_PORT_MIN);

	buf -= BXIPKT_UDP_HDR_SIZE;
	buf_len += BXIPKT_UDP_HDR_SIZE;

	/* Build bxipkt headers */
	len = sizeof(BXIPKT_MAGIC_NUMBER);
	tmp = BXIPKT_MAGIC_NUMBER;
	memcpy(buf, &tmp, len);
	memcpy(buf + len, hdr_data, sizeof(*hdr_data));

	dump_sockaddr_in(__func__, &si_other);

	len = sendto(iface->sockfd, buf, buf_len, 0, (struct sockaddr *)&si_other,
		     sizeof(si_other));

	if (len < 0) {
		log_level = 0;

		if (errno == EAGAIN)
			log_level = 2;
		else if (errno == EPERM)
			log_level = 1;

		if (log_level >= 0)
			LOGN(log_level, "sendto call error (buf_len=%lu): %s\n", buf_len,
			     strerror(errno));

		return 0;
	}

	LOGN(3, "Send: buf_len=%lu, len=%d\n", buf_len, len);
	return 1;
}

/*
 * Post a "inline" PUT command (without SEND event).
 */
int bxipktudp_send_inline(struct bxipkt_iface *iface, struct bximsg_hdr *hdr_data, int nid, int pid)
{
	int ret;
	char buf[BXIPKT_UDP_HDR_SIZE] = { 0 };

	LOGN(3, "%s: hdr.data_seq=%d hdr.ack_seq=%d\n", __func__, hdr_data->data_seq,
	     hdr_data->ack_seq);

	ret = bxipktudp_common_send(iface, buf + BXIPKT_UDP_HDR_SIZE, 0, hdr_data, nid, pid);
	if (ret != 0)
		iface->iopkts++;

	return ret;
}

/*
 * Post a PUT command.
 */
int bxipktudp_send(struct bxipkt_iface *iface, struct bxipkt_buf *b, size_t len, int nid, int pid)
{
	int ret;

	LOGN(3, "%s: nid=%d, pid=%d\n", __func__, nid, pid);

	ret = bxipktudp_common_send(iface, b->addr, len, &b->hdr, nid, pid);

	if (ret != 0) {
		iface->opkts++;
		iface->apkts++;

		if (iface->sent_pkt)
			iface->sent_pkt(b);

		if (iface->output != NULL)
			iface->output(iface->arg, b);
	}

	return ret;
}

struct bxipkt_buf *bxipktudp_getbuf(struct bxipkt_iface *iface)
{
	struct bxipkt_buflist *l = &iface->tx_buflist;
	struct bxipkt_buf *b;

	if (l->freelist == NULL)
		return NULL;

	b = l->freelist;
	l->freelist = b->next;

	return b;
}

void bxipktudp_putbuf(struct bxipkt_iface *iface, struct bxipkt_buf *b)
{
	struct bxipkt_buflist *l = &iface->tx_buflist;

	b->next = l->freelist;
	l->freelist = b;
}

int bxipktudp_rx_progress(struct bxipkt_iface *iface)
{
	int len;
	struct sockaddr_in client_address;
	socklen_t client_address_len;
	int nid = 0;
	int pid = 0;
	unsigned char *p;
	uint32_t tmp;

	for (;;) {
		client_address_len = sizeof(struct sockaddr_in);

		len = recvfrom(iface->sockfd, iface->rx_buf, iface->rx_bufsize, 0,
			       (struct sockaddr *)&client_address, &client_address_len);
		if (len < 0) {
			if (errno == EAGAIN)
				break;
			LOGN(0, "%s: can't receive from %s:%d: %s\n", __func__,
			     inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port),
			     strerror(errno));
			return POLLHUP;
		}

		tmp = BXIPKT_MAGIC_NUMBER;
		/* Check bxipkt UDP magic number */
		if (len < sizeof(BXIPKT_MAGIC_NUMBER) ||
		    memcmp(iface->rx_buf, &tmp, sizeof(BXIPKT_MAGIC_NUMBER)) != 0) {
			LOGN(2, "%s: magic number not found from %s:%d\n", __func__,
			     inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
			continue;
		}

		if (client_address.sin_family != AF_INET) {
			LOGN(0, "%s: not inet address family from %s:%d\n", __func__,
			     inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
			continue;
		}

		pid = ntohs(client_address.sin_port) - BXIPKT_UDP_PORT_MIN;
		if (pid < 0 || pid > PTL_PID_MAX) {
			LOGN(0, "%s: %d: pid out of range from %s:%d\n", __func__, pid,
			     inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
			continue;
		}

		nid = ntohl(client_address.sin_addr.s_addr);
		if (((nid ^ iface->net_addr) & iface->net_mask) != 0) {
			LOGN(0, "%s: client IP (%s:%d) not in the expected network\n", __func__,
			     inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
			continue;
		}

		nid &= ~iface->net_mask;
		if (nid < 0 || nid >= (1 << 24)) {
			LOGN(0, "%s: %d: nid out of range from %s:%d\n", __func__, nid,
			     inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
			continue;
		}

		LOGN(3, "received %d bytes from nid (%d, %d), client addr: %s:%d\n", len, nid, pid,
		     inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));

		if (len < BXIPKT_UDP_HDR_SIZE) {
			LOGN(0, "%s: Received message too short from %s:%d\n", __func__,
			     inet_ntoa(client_address.sin_addr), ntohs(client_address.sin_port));
			continue;
		}

		if (len > BXIPKT_UDP_HDR_SIZE)
			iface->ipkts++;
		else
			iface->iipkts++;

		if (iface->input != NULL) {
			p = iface->rx_buf + sizeof(BXIPKT_MAGIC_NUMBER);
			iface->input(iface->arg, iface->rx_buf + BXIPKT_UDP_HDR_SIZE,
				     len - BXIPKT_UDP_HDR_SIZE, (struct bximsg_hdr *)p, nid, pid,
				     geteuid());
		}
	}

	return 0;
}

void bxipktudp_done(struct bxipkt_iface *iface)
{
#ifdef DEBUG
	if (bxipkt_debug >= 2 || bxipkt_stats) {
		ptl_log("%s: ipkts = %lu, opkts = %lu, iipkts = %lu, iopkts = %lu\n", __func__,
			iface->ipkts, iface->opkts, iface->iipkts, iface->iopkts);
	}
#endif
	free(iface->rx_buf);
	if (iface->sockfd >= 0) {
		shutdown(iface->sockfd, SHUT_RDWR);
		close(iface->sockfd);
	}

	bxipktudp_buflist_done(&iface->tx_buflist);
	free(iface);
}

struct bxipkt_iface *
bxipktudp_init(int service, int nic_iface, int pid, int nbufs, void *arg,
	       void (*input)(void *, void *, size_t, struct bximsg_hdr *, int, int, int),
	       void (*output)(void *, struct bxipkt_buf *),
	       void (*sent_pkt)(struct bxipkt_buf *pkt), int *rnid, int *rpid, int *rmtu)
{
	struct bxipkt_iface *iface;
	int port;
	char err_msg[PTL_LOG_BUF_SIZE];

	if (nbufs <= 0) {
		LOGN(0, "%s: invalid number of buffers\n", __func__);
		return NULL;
	}

	iface = calloc(1, sizeof(struct bxipkt_iface));
	if (iface == NULL) {
		LOGN(0, "malloc(%s): %s\n", __func__, strerror(errno));
		return NULL;
	}

	iface->arg = arg;
	iface->input = input;
	iface->output = output;
	iface->sent_pkt = sent_pkt;
	iface->pid = -1;
	iface->sockfd = -1;
	iface->tx_buflist.count = nbufs;

	if (!bxipktudp_netconfig(iface)) {
		bxipktudp_done(iface);
		return NULL;
	}

	if (!bxipktudp_buflist_init(&iface->tx_buflist)) {
		LOGN(0, "Failed to initialize the list of buffers\n");
		bxipktudp_done(iface);
		return NULL;
	}

	if (pid == PTL_PID_ANY) {
		for (port = BXIPKT_UDP_PORT_MIN + 1; port <= BXIPKT_UDP_PORT_MIN + PTL_PID_MAX;
		     port++) {
			iface->sockfd = bxipktudp_createsocket(iface, port, err_msg);
			if (iface->sockfd >= 0) {
				iface->pid = port - BXIPKT_UDP_PORT_MIN;
				break;
			}
		}
	} else {
		if (pid < 0 || pid > PTL_PID_MAX) {
			LOGN(0, "requested pid out of range\n");
			bxipktudp_done(iface);
			return NULL;
		}
		port = pid + BXIPKT_UDP_PORT_MIN;
		iface->sockfd = bxipktudp_createsocket(iface, port, err_msg);
		if (iface->sockfd >= 0)
			iface->pid = pid;
	}

	if (iface->pid < 0) {
		LOGN(0, "Failed to create server socket: %s\n", err_msg);
		bxipktudp_done(iface);
		return NULL;
	}

	iface->rx_buf = malloc(iface->rx_bufsize);
	if (iface->rx_buf == NULL) {
		LOG("%s: malloc %s size %zd\n", __func__, strerror(errno), iface->rx_bufsize);
		bxipktudp_done(iface);
		return NULL;
	}

	*rnid = iface->nid;
	*rpid = iface->pid;
	*rmtu = iface->tx_buflist.size;

	return iface;
}

void bxipktudp_dump(struct bxipkt_iface *iface)
{
	ptl_log("ipkts = %lu, opkts = %lu, iipkts = %lu, iopkts = %lu, apkts = %lu\n", iface->ipkts,
		iface->opkts, iface->iipkts, iface->iopkts, iface->apkts);
}

int bxipktudp_nfds(struct bxipkt_iface *iface)
{
	return 1;
}

int bxipktudp_pollfd(struct bxipkt_iface *iface, struct pollfd *pfds, int events)
{
	pfds[0].fd = iface->sockfd;
	pfds[0].events = POLLIN | events;

	return 1;
}

int bxipktudp_revents(struct bxipkt_iface *iface, struct pollfd *pfds)
{
	if (pfds[0].revents & POLLHUP)
		return POLLHUP;

	if (pfds[0].revents & POLLIN) {
		if (bxipktudp_rx_progress(iface) & POLLHUP)
			return POLLHUP;
	}

	return pfds[0].revents & POLLOUT;
}

struct bxipkt_ops bxipkt_udp = { bxipktudp_libinit, bxipktudp_libfini, bxipktudp_init,
				 bxipktudp_done,    bxipktudp_send,    bxipktudp_send_inline,
				 bxipktudp_getbuf,  bxipktudp_putbuf,  bxipktudp_dump,
				 bxipktudp_nfds,    bxipktudp_pollfd,  bxipktudp_revents };
