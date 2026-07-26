#ifndef KERNEL_NET_H
#define KERNEL_NET_H

#include "types.h"

void net_init(void);
void net_poll(void);
void net_start_dhcp(void);
int net_socket(int type);
int net_bind(int fd, uint32_t ip, uint16_t port);
int net_listen(int fd, int backlog);
int net_accept(int fd);
int net_connect(int fd, uint32_t ip, uint16_t port);
int net_send(int fd, const void *buf, size_t n);
int net_recv(int fd, void *buf, size_t n);
int net_close(int fd);
uint32_t net_dns(const char *host);
int net_ifconfig(char *buf, size_t n);
int net_ping(uint32_t ip);
uint32_t net_ip(void);
uint32_t net_dns_server(void);

#define NET_SOCK_UDP 1
#define NET_SOCK_TCP 2
#define NET_SOCK_RAW 3

#endif
