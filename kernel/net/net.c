#include "net.h"
#include "pci.h"
#include "mm.h"
#include "string.h"
#include "tty.h"
#include "timer.h"
#include "io.h"
#include "vfs.h"

#define HTONS(x) ((uint16_t)((((x) & 0xFF) << 8) | (((x) >> 8) & 0xFF)))
#define HTONL(x) ((uint32_t)((((x) & 0xFF) << 24) | (((x) & 0xFF00) << 8) | \
                    (((x) & 0xFF0000) >> 8) | (((x) >> 24) & 0xFF)))
#define NTOHS HTONS
#define NTOHL HTONL

#define ETH_IP 0x0800
#define ETH_ARP 0x0806
#define IP_ICMP 1
#define IP_TCP 6
#define IP_UDP 17

typedef struct {
    uint16_t flags;
    uint16_t len;
    uint32_t addr;
} __attribute__((packed)) VRingDesc;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[64];
} __attribute__((packed)) VRingAvail;

typedef struct {
    uint32_t id;
    uint32_t len;
} __attribute__((packed)) VRingUsedElem;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    VRingUsedElem ring[64];
} __attribute__((packed)) VRingUsed;

static uint16_t vio_iobase;
static uint8_t mac[6];
static uint32_t ip_addr;
static uint32_t ip_gw;
static uint32_t ip_mask;
static uint32_t ip_dns;
static int net_ready;

static VRingDesc *rx_desc;
static VRingAvail *rx_avail;
static VRingUsed *rx_used;
static VRingDesc *tx_desc;
static VRingAvail *tx_avail;
static VRingUsed *tx_used;
static uint8_t *rx_bufs[16];
static uint8_t *tx_bufs[16];
static uint16_t rx_idx;
static uint16_t tx_idx;
static uint16_t rx_last_used;
static uint16_t tx_last_used;

static uint8_t arp_mac[6];
static uint32_t arp_ip;
static int arp_valid;

typedef struct {
    int used;
    int type;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;
    int listening;
    int connected;
    uint8_t rbuf[2048];
    uint32_t rlen;
    uint32_t tcp_seq;
    uint32_t tcp_ack;
    int tcp_state;
} Sock;

static Sock socks[16];
static uint16_t ephemeral = 40000;

static uint16_t inet_checksum(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += ((uint32_t)p[0] << 8) | p[1];
        p += 2;
        len -= 2;
    }
    if (len)
        sum += (uint32_t)p[0] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

static int virtio_init(void)
{
    uint8_t bus, slot, func;
    uint32_t bar0, i;
    uint8_t *page;

    if (pci_find(0x1AF4, 0x1000, &bus, &slot, &func) < 0) {
        if (pci_find(0x1AF4, 0x1041, &bus, &slot, &func) < 0)
            return -1;
    }

    bar0 = pci_read(bus, slot, func, 0x10);
    if (!(bar0 & 1))
        return -1;
    vio_iobase = (uint16_t)(bar0 & ~1u);

    {
        uint32_t cmd = pci_read(bus, slot, func, 0x04);
        pci_write(bus, slot, func, 0x04, cmd | 5);
    }

    outb(vio_iobase + 18, 0);
    outb(vio_iobase + 18, 1);
    outb(vio_iobase + 18, 3);

    for (i = 0; i < 6; i++)
        mac[i] = inb(vio_iobase + 0x14 + (uint16_t)i);

    page = (uint8_t *)mm_alloc_pages(8);
    if (!page)
        return -1;
    kmemset(page, 0, 8 * PAGE_SIZE);

    rx_desc = (VRingDesc *)page;
    rx_avail = (VRingAvail *)(page + 0x200);
    rx_used = (VRingUsed *)(page + 0x400);
    tx_desc = (VRingDesc *)(page + 0x800);
    tx_avail = (VRingAvail *)(page + 0xA00);
    tx_used = (VRingUsed *)(page + 0xC00);

    for (i = 0; i < 16; i++) {
        rx_bufs[i] = page + 0x1000 + i * 1520;
        tx_bufs[i] = page + 0x1000 + 16 * 1520 + i * 1520;
        rx_desc[i].addr = (uint32_t)rx_bufs[i];
        rx_desc[i].len = 1520;
        rx_desc[i].flags = 2;
        rx_avail->ring[i] = (uint16_t)i;
    }
    rx_avail->idx = 16;
    rx_idx = 16;
    tx_idx = 0;
    rx_last_used = 0;
    tx_last_used = 0;

    outw(vio_iobase + 14, 0);
    outl(vio_iobase + 8, (uint32_t)rx_desc >> 12);
    outw(vio_iobase + 14, 1);
    outl(vio_iobase + 8, (uint32_t)tx_desc >> 12);
    outb(vio_iobase + 18, 7);
    return 0;
}

static uint8_t tx_frame[1514];
static int net_polling;

static void arp_send(uint32_t tip, int reply, const uint8_t *tha);

static void eth_send(const void *frame, size_t len)
{
    uint16_t i = tx_idx % 16;
    if (!vio_iobase)
        return;
    if (!tx_bufs[i]) {
        tty_writeln("eth: nobuf");
        return;
    }
    if (len > 1500)
        len = 1500;
    kmemcpy(tx_bufs[i] + 10, frame, len);
    kmemset(tx_bufs[i], 0, 10);
    tx_desc[i].addr = (uint32_t)tx_bufs[i];
    tx_desc[i].len = (uint16_t)(len + 10);
    tx_desc[i].flags = 0;
    tx_avail->ring[tx_idx % 64] = i;
    tx_idx++;
    tx_avail->idx = tx_idx;
    outw(vio_iobase + 16, 1);
}

static int ensure_arp(uint32_t tip)
{
    int i;
    if (tip == 0xFFFFFFFFu)
        return 0;
    if (arp_valid && arp_ip == tip)
        return 0;
    for (i = 0; i < 50; i++) {
        if (arp_valid && arp_ip == tip)
            return 0;
        arp_send(tip, 0, 0);
        net_poll();
    }
    return (arp_valid && arp_ip == tip) ? 0 : -1;
}

static uint32_t route_nexthop(uint32_t dst)
{
    if (dst == 0xFFFFFFFFu)
        return dst;
    if (ip_mask && ((dst & ip_mask) == (ip_addr & ip_mask)))
        return dst;
    if (ip_gw)
        return ip_gw;
    return dst;
}

static void arp_send(uint32_t tip, int reply, const uint8_t *tha)
{
    uint8_t f[64];
    kmemset(f, 0, sizeof(f));
    kmemset(f, 0xFF, 6);
    if (reply && tha)
        kmemcpy(f, tha, 6);
    kmemcpy(f + 6, mac, 6);
    f[12] = 0x08;
    f[13] = 0x06;
    f[15] = 1;
    f[16] = 0x08;
    f[18] = 6;
    f[19] = 4;
    f[21] = reply ? 2 : 1;
    kmemcpy(f + 22, mac, 6);
    f[28] = (uint8_t)(ip_addr >> 24);
    f[29] = (uint8_t)(ip_addr >> 16);
    f[30] = (uint8_t)(ip_addr >> 8);
    f[31] = (uint8_t)ip_addr;
    if (tha)
        kmemcpy(f + 32, tha, 6);
    f[38] = (uint8_t)(tip >> 24);
    f[39] = (uint8_t)(tip >> 16);
    f[40] = (uint8_t)(tip >> 8);
    f[41] = (uint8_t)tip;
    eth_send(f, 42);
}

static void ip_send(uint32_t dst, uint8_t proto, const void *payload, size_t plen)
{
    uint8_t *f = tx_frame;
    uint16_t total = (uint16_t)(20 + plen);
    uint8_t bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    uint8_t *dstmac = 0;
    uint32_t arp_target = dst;

    if (plen > sizeof(tx_frame) - 34)
        return;

    if (dst == 0xFFFFFFFFu)
        dstmac = bcast;
    else if (arp_valid && arp_ip == dst)
        dstmac = arp_mac;
    else if (ip_mask && ((dst & ip_mask) == (ip_addr & ip_mask))) {
        arp_send(dst, 0, 0);
        return;
    } else if (ip_gw) {
        arp_target = ip_gw;
        if (!(arp_valid && arp_ip == ip_gw)) {
            arp_send(ip_gw, 0, 0);
            return;
        }
        dstmac = arp_mac;
    } else {
        arp_send(dst, 0, 0);
        return;
    }
    (void)arp_target;
    kmemcpy(f, dstmac, 6);
    kmemcpy(f + 6, mac, 6);
    f[12] = 0x08;
    f[13] = 0x00;
    f[14] = 0x45;
    f[15] = 0;
    f[16] = (uint8_t)(total >> 8);
    f[17] = (uint8_t)total;
    f[18] = 0;
    f[19] = 1;
    f[20] = 0;
    f[21] = 0;
    f[22] = 64;
    f[23] = proto;
    f[24] = 0;
    f[25] = 0;
    f[26] = (uint8_t)(ip_addr >> 24);
    f[27] = (uint8_t)(ip_addr >> 16);
    f[28] = (uint8_t)(ip_addr >> 8);
    f[29] = (uint8_t)ip_addr;
    f[30] = (uint8_t)(dst >> 24);
    f[31] = (uint8_t)(dst >> 16);
    f[32] = (uint8_t)(dst >> 8);
    f[33] = (uint8_t)dst;
    if (dst == 0xFFFFFFFFu) {
        f[30] = 0xff;
        f[31] = 0xff;
        f[32] = 0xff;
        f[33] = 0xff;
    }
    {
        uint16_t csum = inet_checksum(f + 14, 20);
        f[24] = (uint8_t)(csum >> 8);
        f[25] = (uint8_t)csum;
    }
    kmemcpy(f + 34, payload, plen);
    eth_send(f, 14 + total);
}

static void udp_send(uint32_t dst, uint16_t sport, uint16_t dport, const void *data, size_t n)
{
    static uint8_t buf[1480];
    uint16_t len = (uint16_t)(8 + n);
    if (n > sizeof(buf) - 8)
        n = sizeof(buf) - 8;
    len = (uint16_t)(8 + n);
    buf[0] = (uint8_t)(sport >> 8);
    buf[1] = (uint8_t)sport;
    buf[2] = (uint8_t)(dport >> 8);
    buf[3] = (uint8_t)dport;
    buf[4] = (uint8_t)(len >> 8);
    buf[5] = (uint8_t)len;
    buf[6] = 0;
    buf[7] = 0;
    kmemcpy(buf + 8, data, n);
    ip_send(dst, IP_UDP, buf, len);
}

static void handle_udp(uint32_t src, const uint8_t *udp, size_t len)
{
    uint16_t sport = (uint16_t)((udp[0] << 8) | udp[1]);
    uint16_t dport = (uint16_t)((udp[2] << 8) | udp[3]);
    uint16_t ulen = (uint16_t)((udp[4] << 8) | udp[5]);
    int i;
    if (ulen < 8 || ulen > len)
        return;
    for (i = 0; i < 16; i++) {
        if (!socks[i].used || socks[i].type != NET_SOCK_UDP)
            continue;
        if (socks[i].local_port == dport) {
            size_t pay = ulen - 8;
            if (pay > sizeof(socks[i].rbuf) - socks[i].rlen)
                pay = sizeof(socks[i].rbuf) - socks[i].rlen;
            kmemcpy(socks[i].rbuf + socks[i].rlen, udp + 8, pay);
            socks[i].rlen += (uint32_t)pay;
            socks[i].remote_ip = src;
            socks[i].remote_port = sport;
        }
    }
}

static void handle_icmp(uint32_t src, const uint8_t *icmp, size_t len)
{
    static uint8_t reply[64];
    if (len < 8 || icmp[0] != 8)
        return;
    kmemcpy(reply, icmp, len > 64 ? 64 : len);
    reply[0] = 0;
    reply[2] = 0;
    reply[3] = 0;
    {
        uint16_t c = inet_checksum(reply, len > 64 ? 64 : len);
        reply[2] = (uint8_t)(c >> 8);
        reply[3] = (uint8_t)c;
    }
    ip_send(src, IP_ICMP, reply, len > 64 ? 64 : len);
}

static void tcp_send_raw(uint32_t dst, uint16_t sport, uint16_t dport, uint32_t seq,
                         uint32_t ack, uint8_t flags, const void *data, size_t n)
{
    static uint8_t buf[1480];
    uint16_t len = (uint16_t)(20 + n);
    if (n > sizeof(buf) - 20)
        n = sizeof(buf) - 20;
    len = (uint16_t)(20 + n);
    kmemset(buf, 0, 20);
    buf[0] = (uint8_t)(sport >> 8);
    buf[1] = (uint8_t)sport;
    buf[2] = (uint8_t)(dport >> 8);
    buf[3] = (uint8_t)dport;
    buf[4] = (uint8_t)(seq >> 24);
    buf[5] = (uint8_t)(seq >> 16);
    buf[6] = (uint8_t)(seq >> 8);
    buf[7] = (uint8_t)seq;
    buf[8] = (uint8_t)(ack >> 24);
    buf[9] = (uint8_t)(ack >> 16);
    buf[10] = (uint8_t)(ack >> 8);
    buf[11] = (uint8_t)ack;
    buf[12] = 0x50;
    buf[13] = flags;
    buf[14] = 0xFF;
    buf[15] = 0xFF;
    if (n)
        kmemcpy(buf + 20, data, n);
    ip_send(dst, IP_TCP, buf, len);
}

static void handle_tcp(uint32_t src, const uint8_t *tcp, size_t len)
{
    uint16_t sport = (uint16_t)((tcp[0] << 8) | tcp[1]);
    uint16_t dport = (uint16_t)((tcp[2] << 8) | tcp[3]);
    uint32_t seq = ((uint32_t)tcp[4] << 24) | ((uint32_t)tcp[5] << 16) |
                   ((uint32_t)tcp[6] << 8) | tcp[7];
    uint8_t flags = tcp[13];
    size_t hdr = (tcp[12] >> 4) * 4;
    size_t pay = len > hdr ? len - hdr : 0;
    int i;
    for (i = 0; i < 16; i++) {
        Sock *s = &socks[i];
        if (!s->used || s->type != NET_SOCK_TCP)
            continue;
        if (s->listening && s->local_port == dport && (flags & 0x02)) {
            s->remote_ip = src;
            s->remote_port = sport;
            s->tcp_ack = seq + 1;
            s->tcp_seq = 1;
            s->connected = 1;
            s->tcp_state = 1;
            tcp_send_raw(src, dport, sport, s->tcp_seq, s->tcp_ack, 0x12, 0, 0);
            s->tcp_seq++;
            continue;
        }
        if (s->local_port == dport && s->remote_ip == src && s->remote_port == sport) {
            
            if ((flags & 0x12) == 0x12) {
                s->tcp_ack = seq + 1;
                tcp_send_raw(src, dport, sport, s->tcp_seq, s->tcp_ack, 0x10, 0, 0);
                s->connected = 1;
                s->tcp_state = 2;
            } else if (flags & 0x10) {
                s->connected = 1;
            }
            if (pay) {
                if (pay > sizeof(s->rbuf) - s->rlen)
                    pay = sizeof(s->rbuf) - s->rlen;
                kmemcpy(s->rbuf + s->rlen, tcp + hdr, pay);
                s->rlen += (uint32_t)pay;
                s->tcp_ack = seq + (uint32_t)pay;
                tcp_send_raw(src, dport, sport, s->tcp_seq, s->tcp_ack, 0x10, 0, 0);
            }
            if (flags & 0x01) {
                s->tcp_ack = seq + 1;
                tcp_send_raw(src, dport, sport, s->tcp_seq, s->tcp_ack, 0x10, 0, 0);
                s->connected = 0;
            }
        }
    }
}

static void handle_frame(uint8_t *frame, size_t len)
{
    uint16_t ethertype;
    if (len < 14)
        return;
    ethertype = (uint16_t)((frame[12] << 8) | frame[13]);
    if (ethertype == ETH_ARP && len >= 42) {
        uint16_t op = (uint16_t)((frame[20] << 8) | frame[21]);
        uint32_t sip = ((uint32_t)frame[28] << 24) | ((uint32_t)frame[29] << 16) |
                       ((uint32_t)frame[30] << 8) | frame[31];
        uint32_t tip = ((uint32_t)frame[38] << 24) | ((uint32_t)frame[39] << 16) |
                       ((uint32_t)frame[40] << 8) | frame[41];
        if (op == 1 && tip == ip_addr)
            arp_send(sip, 1, frame + 22);
        if (op == 2) {
            kmemcpy(arp_mac, frame + 22, 6);
            arp_ip = sip;
            arp_valid = 1;
        }
        return;
    }
    if (ethertype == ETH_IP && len >= 34) {
        uint8_t ihl = (frame[14] & 0xF) * 4;
        uint8_t proto = frame[23];
        uint32_t src = ((uint32_t)frame[26] << 24) | ((uint32_t)frame[27] << 16) |
                       ((uint32_t)frame[28] << 8) | frame[29];
        uint16_t total = (uint16_t)((frame[16] << 8) | frame[17]);
        const uint8_t *payload = frame + 14 + ihl;
        size_t plen = total > ihl ? total - ihl : 0;
        if (14 + total > len)
            return;
        if (proto == IP_ICMP)
            handle_icmp(src, payload, plen);
        else if (proto == IP_UDP)
            handle_udp(src, payload, plen);
        else if (proto == IP_TCP)
            handle_tcp(src, payload, plen);
    }
}

void net_poll(void)
{
    int guard = 0;
    if (!vio_iobase || net_polling)
        return;
    net_polling = 1;
    while (rx_last_used != rx_used->idx && guard++ < 32) {
        VRingUsedElem *e = &rx_used->ring[rx_last_used % 64];
        uint8_t *buf = rx_bufs[e->id];
        if (e->len > 10)
            handle_frame(buf + 10, e->len - 10);
        rx_avail->ring[rx_idx % 64] = (uint16_t)e->id;
        rx_idx++;
        rx_avail->idx = rx_idx;
        rx_last_used++;
    }
    net_polling = 0;
}

void net_start_dhcp(void)
{
    char buf[256];
    int n, i, ls;
    uint32_t nip = 0x0A00020F, ngw = 0x0A000202, nmask = 0xFFFFFF00, ndns = 0x0A000203;

    n = vfs_read_file("/sys/etc/network.conf", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        ls = 0;
        for (i = 0; i <= n; i++) {
            char line[80];
            int len, a, b, c, d;
            uint32_t *dst = 0;
            if (i < n && buf[i] != '\n' && buf[i] != '\r')
                continue;
            len = i - ls;
            if (len > 0 && buf[ls + len - 1] == '\r')
                len--;
            if (len <= 0) {
                ls = i + 1;
                continue;
            }
            if (len >= (int)sizeof(line))
                len = (int)sizeof(line) - 1;
            kmemcpy(line, buf + ls, (size_t)len);
            line[len] = 0;
            ls = i + 1;
            if (line[0] == '#' || !line[0])
                continue;
            if (line[0] == 'i' && line[1] == 'p' && line[2] == '=')
                dst = &nip;
            else if (line[0] == 'g' && line[1] == 'w' && line[2] == '=')
                dst = &ngw;
            else if (line[0] == 'm' && line[1] == 'a' && line[2] == 's')
                dst = &nmask;
            else if (line[0] == 'd' && line[1] == 'n' && line[2] == 's')
                dst = &ndns;
            if (!dst)
                continue;
            /* find first digit */
            {
                char *p = line;
                while (*p && (*p < '0' || *p > '9'))
                    p++;
                a = b = c = d = 0;
                if (p[0]) {
                    /* crude parse a.b.c.d */
                    a = 0;
                    while (*p >= '0' && *p <= '9')
                        a = a * 10 + (*p++ - '0');
                    if (*p == '.')
                        p++;
                    while (*p >= '0' && *p <= '9')
                        b = b * 10 + (*p++ - '0');
                    if (*p == '.')
                        p++;
                    while (*p >= '0' && *p <= '9')
                        c = c * 10 + (*p++ - '0');
                    if (*p == '.')
                        p++;
                    while (*p >= '0' && *p <= '9')
                        d = d * 10 + (*p++ - '0');
                    *dst = ((uint32_t)a << 24) | ((uint32_t)b << 16) |
                           ((uint32_t)c << 8) | (uint32_t)d;
                }
            }
        }
    }

    ip_addr = nip;
    ip_gw = ngw;
    ip_mask = nmask;
    ip_dns = ndns;
    net_ready = 1;
    tty_writeln("[netd] iface from network.conf (or SLIRP defaults)");
}

void net_init(void)
{
    int s;
    kmemset(socks, 0, sizeof(socks));
    ip_addr = 0;
    net_ready = 0;
    if (virtio_init() == 0)
        tty_writeln("[net] virtio-net ready");
    else
        tty_writeln("[net] no virtio-net");
    s = net_socket(NET_SOCK_UDP);
    if (s >= 0)
        net_bind(s, 0, 68);
}

int net_socket(int type)
{
    int i;
    for (i = 0; i < 16; i++) {
        if (!socks[i].used) {
            kmemset(&socks[i], 0, sizeof(socks[i]));
            socks[i].used = 1;
            socks[i].type = type;
            return i;
        }
    }
    return -1;
}

int net_bind(int fd, uint32_t ip, uint16_t port)
{
    (void)ip;
    if (fd < 0 || fd >= 16 || !socks[fd].used)
        return -1;
    socks[fd].local_port = port;
    return 0;
}

int net_listen(int fd, int backlog)
{
    (void)backlog;
    if (fd < 0 || fd >= 16 || !socks[fd].used)
        return -1;
    socks[fd].listening = 1;
    socks[fd].type = NET_SOCK_TCP;
    return 0;
}

int net_accept(int fd)
{
    uint32_t start = timer_ticks();
    if (fd < 0 || fd >= 16 || !socks[fd].used)
        return -1;
    while (timer_ticks() - start < 1000) {
        net_poll();
        if (socks[fd].connected)
            return fd;
    }
    return -1;
}

int net_connect(int fd, uint32_t ip, uint16_t port)
{
    uint32_t syn_seq = 1;
    uint32_t nh;
    int tries;
    if (fd < 0 || fd >= 16 || !socks[fd].used)
        return -1;
    socks[fd].type = NET_SOCK_TCP;
    socks[fd].remote_ip = ip;
    socks[fd].remote_port = port;
    socks[fd].connected = 0;
    socks[fd].rlen = 0;
    if (!socks[fd].local_port)
        socks[fd].local_port = ephemeral++;
    socks[fd].tcp_seq = syn_seq;
    socks[fd].tcp_ack = 0;
    socks[fd].tcp_state = 0;
    nh = route_nexthop(ip);
    for (tries = 0; tries < 16; tries++) {
        if (arp_valid && arp_ip == nh)
            break;
        arp_send(nh, 0, 0);
        net_poll();
    }
    if (!(arp_valid && arp_ip == nh) && nh != 0xFFFFFFFFu)
        return -1;
    for (tries = 0; tries < 400; tries++) {
        if ((tries % 40) == 0) {
            tcp_send_raw(ip, socks[fd].local_port, port, syn_seq, 0, 0x02, 0, 0);
            socks[fd].tcp_seq = syn_seq + 1;
        }
        net_poll();
        if (socks[fd].connected)
            return 0;
    }
    return -1;
}

int net_send(int fd, const void *buf, size_t n)
{
    net_poll();
    if (fd < 0 || fd >= 16 || !socks[fd].used)
        return -1;
    if (socks[fd].type == NET_SOCK_UDP) {
        udp_send(socks[fd].remote_ip, socks[fd].local_port, socks[fd].remote_port, buf, n);
        return (int)n;
    }
    if (socks[fd].type == NET_SOCK_TCP) {
        tcp_send_raw(socks[fd].remote_ip, socks[fd].local_port, socks[fd].remote_port,
                     socks[fd].tcp_seq, socks[fd].tcp_ack, 0x18, buf, n);
        socks[fd].tcp_seq += (uint32_t)n;
        return (int)n;
    }
    return -1;
}

int net_recv(int fd, void *buf, size_t n)
{
    int spins;
    if (fd < 0 || fd >= 16 || !socks[fd].used)
        return -1;
    for (spins = 0; socks[fd].rlen == 0 && socks[fd].connected && spins < 20000; spins++)
        net_poll();
    if (!socks[fd].rlen)
        return socks[fd].connected ? 0 : -1;
    if (n > socks[fd].rlen)
        n = socks[fd].rlen;
    kmemcpy(buf, socks[fd].rbuf, n);
    kmemcpy(socks[fd].rbuf, socks[fd].rbuf + n, socks[fd].rlen - n);
    socks[fd].rlen -= (uint32_t)n;
    return (int)n;
}

int net_close(int fd)
{
    if (fd < 0 || fd >= 16 || !socks[fd].used)
        return -1;
    if (socks[fd].type == NET_SOCK_TCP && socks[fd].connected) {
        tcp_send_raw(socks[fd].remote_ip, socks[fd].local_port, socks[fd].remote_port,
                     socks[fd].tcp_seq, socks[fd].tcp_ack, 0x11, 0, 0);
    }
    kmemset(&socks[fd], 0, sizeof(socks[fd]));
    return 0;
}

uint32_t net_dns(const char *host)
{
    static uint8_t q[512];
    int s, n, i, j;
    uint16_t id = 0x1234;
    kmemset(q, 0, sizeof(q));
    q[0] = (uint8_t)(id >> 8);
    q[1] = (uint8_t)id;
    q[2] = 1;
    q[5] = 1;
    i = 12;
    j = 0;
    while (host[j]) {
        int start = j;
        int len;
        while (host[j] && host[j] != '.')
            j++;
        len = j - start;
        q[i++] = (uint8_t)len;
        kmemcpy(q + i, host + start, (size_t)len);
        i += len;
        if (host[j] == '.')
            j++;
    }
    q[i++] = 0;
    q[i++] = 0;
    q[i++] = 1;
    q[i++] = 0;
    q[i++] = 1;
    s = net_socket(NET_SOCK_UDP);
    if (s < 0)
        return 0;
    net_bind(s, 0, ephemeral++);
    socks[s].remote_ip = ip_dns ? ip_dns : 0x08080808;
    socks[s].remote_port = 53;
    net_send(s, q, (size_t)i);
    n = net_recv(s, q, sizeof(q));
    socks[s].used = 0;
    if (n < 16)
        return 0;
    for (j = n - 4; j >= 12; j--) {
        if (q[j] == 0 && j + 4 < n)
            return ((uint32_t)q[j] << 24) | ((uint32_t)q[j + 1] << 16) |
                   ((uint32_t)q[j + 2] << 8) | q[j + 3];
    }
    return 0;
}

static void ip_to_str(uint32_t ip, char *out)
{
    int n = 0;
    int a, b, c, d, i;
    a = (int)((ip >> 24) & 255);
    b = (int)((ip >> 16) & 255);
    c = (int)((ip >> 8) & 255);
    d = (int)(ip & 255);
    {
        int vals[4] = { a, b, c, d };
        for (i = 0; i < 4; i++) {
            char tmp[4];
            int v = vals[i], k = 0;
            if (v == 0)
                tmp[k++] = '0';
            else {
                char rev[4];
                int r = 0;
                while (v) {
                    rev[r++] = (char)('0' + (v % 10));
                    v /= 10;
                }
                while (r)
                    tmp[k++] = rev[--r];
            }
            tmp[k] = 0;
            kmemcpy(out + n, tmp, (size_t)k);
            n += k;
            if (i < 3)
                out[n++] = '.';
        }
        out[n] = 0;
    }
}

int net_ifconfig(char *buf, size_t n)
{
    char ip[16], gw[16], dns[16];
    size_t pos = 0;
    ip_to_str(ip_addr, ip);
    ip_to_str(ip_gw, gw);
    ip_to_str(ip_dns, dns);
#define PUT(s) do { size_t L=kstrlen(s); if (pos+L+1<n){kmemcpy(buf+pos,s,L);pos+=L;} } while(0)
    PUT("eth0 ip ");
    PUT(ip);
    PUT(" gw ");
    PUT(gw);
    PUT(" dns ");
    PUT(dns);
    PUT("\n");
#undef PUT
    buf[pos] = 0;
    return (int)pos;
}

int net_ping(uint32_t ip)
{
    static uint8_t frame[64];
    int tries;
    for (tries = 0; tries < 4; tries++) {
        
        kmemset(frame, 0, sizeof(frame));
        kmemset(frame, 0xff, 6);
        kmemcpy(frame + 6, mac, 6);
        frame[12] = 0x08;
        frame[13] = 0x06;
        frame[15] = 1;
        frame[16] = 0x08;
        frame[18] = 6;
        frame[19] = 4;
        frame[21] = 1;
        kmemcpy(frame + 22, mac, 6);
        frame[28] = (uint8_t)(ip_addr >> 24);
        frame[29] = (uint8_t)(ip_addr >> 16);
        frame[30] = (uint8_t)(ip_addr >> 8);
        frame[31] = (uint8_t)ip_addr;
        frame[38] = (uint8_t)(ip >> 24);
        frame[39] = (uint8_t)(ip >> 16);
        frame[40] = (uint8_t)(ip >> 8);
        frame[41] = (uint8_t)ip;
        eth_send(frame, 42);
        net_poll();
    }
    return 0;
}

uint32_t net_ip(void)
{
    return ip_addr;
}

uint32_t net_dns_server(void)
{
    return ip_dns;
}
