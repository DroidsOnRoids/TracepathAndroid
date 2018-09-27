/*
 * tracepath.c
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/icmp6.h>

#include <linux/types.h>
#include <linux/errqueue.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <limits.h>
#include <resolv.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <arpa/inet.h>

#ifdef USE_IDN
#include <locale.h>

#ifndef AI_IDN
#define AI_IDN 0x0040
#endif
#ifndef NI_IDN
#define NI_IDN 32
#endif

#define getnameinfo_flags	NI_IDN
#else
#define getnameinfo_flags    0
#endif

#ifndef SOL_IPV6
#define SOL_IPV6 IPPROTO_IPV6
#endif

#ifndef IP_PMTUDISC_DO
#define IP_PMTUDISC_DO		3
#endif
#ifndef IPV6_PMTUDISC_DO
#define IPV6_PMTUDISC_DO	3
#endif

#define MAX_HOPS_LIMIT        255
#define MAX_HOPS_DEFAULT    30

struct hhistory {
    int hops;
    struct timeval sendtime;
};

struct hhistory his[64];
int hisptr;

struct sockaddr_storage target;
socklen_t targetlen;
__u16 base_port;
int max_hops = MAX_HOPS_DEFAULT;

int overhead;
size_t mtu;
void *pktbuf;
int hops_to = -1;
int hops_from = -1;
int no_resolve = 0;
int show_both = 0;
int mapped;

#define HOST_COLUMN_SIZE    52

struct probehdr {
    __u32 ttl;
    struct timeval tv;
};

void data_wait(int fd) {
    fd_set fds;
    struct timeval tv;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    select(fd + 1, &fds, NULL, NULL, &tv);
}

void print_host(const char *a, const char *b, int both, FILE *output) {
    int plen;
    plen = fprintf(output, "%s", a);
    if (both)
        plen += fprintf(output, " (%s)", b);
    if (plen >= HOST_COLUMN_SIZE)
        plen = HOST_COLUMN_SIZE - 1;
    fprintf(output, "%*s", HOST_COLUMN_SIZE - plen, "");
}

void fperror(FILE *output, const char *message) {
    fprintf(output, "%s: %s", message, strerror(errno));
}

ssize_t recverr(int fd, struct addrinfo *ai, int ttl, FILE *output) {
    ssize_t res;
    struct probehdr rcvbuf;
    char cbuf[512];
    struct iovec iov;
    struct msghdr msg;
    struct cmsghdr *cmsg;
    struct sock_extended_err *e;
    struct sockaddr_storage addr;
    struct timeval tv;
    struct timeval *rettv;
    int slot = 0;
    int rethops;
    int sndhops;
    ssize_t progress = -1;
    int broken_router;
    char hnamebuf[NI_MAXHOST] = "";

    restart:
    memset(&rcvbuf, -1, sizeof(rcvbuf));
    iov.iov_base = &rcvbuf;
    iov.iov_len = sizeof(rcvbuf);
    msg.msg_name = (__u8 *) &addr;
    msg.msg_namelen = sizeof(addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_flags = 0;
    msg.msg_control = cbuf;
    msg.msg_controllen = sizeof(cbuf);

    gettimeofday(&tv, NULL);
    res = recvmsg(fd, &msg, MSG_ERRQUEUE);
    if (res < 0) {
        if (errno == EAGAIN)
            return progress;
        goto restart;
    }

    progress = mtu;

    rethops = -1;
    sndhops = -1;
    e = NULL;
    rettv = NULL;

    slot = -base_port;
    switch (ai->ai_family) {
        case AF_INET6:
            slot += ntohs(((struct sockaddr_in6 *) &addr)->sin6_port);
            break;
        case AF_INET:
            slot += ntohs(((struct sockaddr_in *) &addr)->sin_port);
            break;
    }

    if (slot >= 0 && slot < 63 && his[slot].hops) {
        sndhops = his[slot].hops;
        rettv = &his[slot].sendtime;
        his[slot].hops = 0;
    }
    broken_router = 0;
    if (res == sizeof(rcvbuf)) {
        if (rcvbuf.ttl == 0 || rcvbuf.tv.tv_sec == 0)
            broken_router = 1;
        else {
            sndhops = rcvbuf.ttl;
            rettv = &rcvbuf.tv;
        }
    }

    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
        switch (cmsg->cmsg_level) {
            case SOL_IPV6:
                switch (cmsg->cmsg_type) {
                    case IPV6_RECVERR:
                        e = (struct sock_extended_err *) CMSG_DATA(cmsg);
                        break;
                    case IPV6_HOPLIMIT:
#ifdef IPV6_2292HOPLIMIT
                    case IPV6_2292HOPLIMIT:
#endif
                        memcpy(&rethops, CMSG_DATA(cmsg), sizeof(rethops));
                        break;
                    default:
                        fprintf(output, "cmsg6:%d\n ", cmsg->cmsg_type);
                }
                break;
            case SOL_IP:
                switch (cmsg->cmsg_type) {
                    case IP_RECVERR:
                        e = (struct sock_extended_err *) CMSG_DATA(cmsg);
                        break;
                    case IP_TTL:
                        rethops = *(__u8 *) CMSG_DATA(cmsg);
                        break;
                    default:
                        fprintf(output, "cmsg4:%d\n ", cmsg->cmsg_type);
                }
        }
    }
    if (e == NULL) {
        fprintf(output, "no info\n");
        return 0;
    }
    if (e->ee_origin == SO_EE_ORIGIN_LOCAL)
        fprintf(output, "%2d?: %-32s ", ttl, "[LOCALHOST]");
    else if (e->ee_origin == SO_EE_ORIGIN_ICMP6 ||
             e->ee_origin == SO_EE_ORIGIN_ICMP) {
        char abuf[NI_MAXHOST];
        struct sockaddr *sa = (struct sockaddr *) (e + 1);
        socklen_t salen;

        if (sndhops > 0)
            fprintf(output, "%2d:  ", sndhops);
        else
            fprintf(output, "%2d?: ", ttl);

        switch (sa->sa_family) {
            case AF_INET6:
                salen = sizeof(struct sockaddr_in6);
                break;
            case AF_INET:
                salen = sizeof(struct sockaddr_in);
                break;
            default:
                salen = 0;
        }

        if (no_resolve || show_both) {
            if (getnameinfo(sa, salen,
                            abuf, sizeof(abuf), NULL, 0,
                            NI_NUMERICHOST))
                strcpy(abuf, "???");
        } else
            abuf[0] = 0;

        if (!no_resolve || show_both) {
            fflush(stdout);
            if (getnameinfo(sa, salen, hnamebuf, sizeof hnamebuf, NULL, 0, getnameinfo_flags))
                strcpy(hnamebuf, "???");
        } else
            hnamebuf[0] = 0;

        if (no_resolve)
            print_host(abuf, hnamebuf, show_both, output);
        else
            print_host(hnamebuf, abuf, show_both, output);
    }

    if (rettv) {
        int diff = (tv.tv_sec - rettv->tv_sec) * 1000000 + (tv.tv_usec - rettv->tv_usec);
        fprintf(output, "%3d.%03dms ", diff / 1000, diff % 1000);
        if (broken_router)
            fprintf(output, "(This broken router returned corrupted payload) ");
    }

    if (rethops <= 64)
        rethops = 65 - rethops;
    else if (rethops <= 128)
        rethops = 129 - rethops;
    else
        rethops = 256 - rethops;

    switch (e->ee_errno) {
        case ETIMEDOUT:
            fprintf(output, "\n");
            break;
        case EMSGSIZE:
            fprintf(output, "pmtu %d\n", e->ee_info);
            mtu = e->ee_info;
            progress = mtu;
            break;
        case ECONNREFUSED:
            fprintf(output, "reached\n");
            hops_to = sndhops < 0 ? ttl : sndhops;
            hops_from = rethops;
            return 0;
        case EPROTO:
            fprintf(output, "!P\n");
            return 0;
        case EHOSTUNREACH:
            if ((e->ee_origin == SO_EE_ORIGIN_ICMP &&
                 e->ee_type == 11 &&
                 e->ee_code == 0) ||
                (e->ee_origin == SO_EE_ORIGIN_ICMP6 &&
                 e->ee_type == 3 &&
                 e->ee_code == 0)) {
                if (rethops >= 0) {
                    if (sndhops >= 0 && rethops != sndhops)
                        fprintf(output, "asymm %2d ", rethops);
                    else if (sndhops < 0 && rethops != ttl)
                        fprintf(output, "asymm %2d ", rethops);
                }
                fprintf(output, "\n");
                break;
            }
            fprintf(output, "!H\n");
            return 0;
        case ENETUNREACH:
            fprintf(output, "!N\n");
            return 0;
        case EACCES:
            fprintf(output, "!A\n");
            return 0;
        default:
            fprintf(output, "\n");
            errno = e->ee_errno;
            fperror(output, "NET ERROR");
            return 0;
    }
    goto restart;
}

ssize_t probe_ttl(int fd, struct addrinfo *ai, uint32_t ttl, FILE *output) {
    int i;
    struct probehdr *hdr = pktbuf;

    memset(pktbuf, 0, mtu);
    restart:
    for (i = 0; i < 10; i++) {
        ssize_t res;

        hdr->ttl = ttl;
        switch (ai->ai_family) {
            case AF_INET6:
                ((struct sockaddr_in6 *) &target)->sin6_port = htons(base_port + hisptr);
                break;
            case AF_INET:
                ((struct sockaddr_in *) &target)->sin_port = htons(base_port + hisptr);
                break;
        }
        gettimeofday(&hdr->tv, NULL);
        his[hisptr].hops = ttl;
        his[hisptr].sendtime = hdr->tv;
        if (sendto(fd, pktbuf, mtu - overhead, 0, (struct sockaddr *) &target, targetlen) > 0)
            break;
        res = recverr(fd, ai, ttl, output);
        his[hisptr].hops = 0;
        if (res == 0)
            return 0;
        if (res > 0)
            goto restart;
    }
    hisptr = (hisptr + 1) & 63;

    if (i < 10) {
        data_wait(fd);
        if (recv(fd, pktbuf, mtu, MSG_DONTWAIT) > 0) {
            fprintf(output, "%2d?: reply received 8)\n", ttl);
            return 0;
        }
        return recverr(fd, ai, ttl, output);
    }

    fprintf(output, "%2d:  send failed\n", ttl);
    return 0;
}

int tracepath_main(char *destination, uint16_t port, FILE *output) {
    struct addrinfo hints = {
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_DGRAM,
            .ai_protocol = IPPROTO_UDP,
#ifdef USE_IDN
            .ai_flags = AI_IDN | AI_CANONNAME,
#endif
    };
    struct addrinfo *ai, *result;
    int status;
    int fd;
    int on;
    uint32_t ttl;
    char pbuf[NI_MAXSERV];

#ifdef USE_IDN
    setlocale(LC_ALL, "");
#endif

    base_port = port;

    sprintf(pbuf, "%u", base_port);

    status = getaddrinfo(destination, pbuf, &hints, &result);
    if (status) {
        fprintf(output, "tracepath: %s: %s\n", destination, gai_strerror(status));
        return 1;
    }

    fd = -1;
    for (ai = result; ai; ai = ai->ai_next) {
        if (ai->ai_family != AF_INET6 &&
            ai->ai_family != AF_INET)
            continue;
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;
        memcpy(&target, ai->ai_addr, ai->ai_addrlen);
        targetlen = ai->ai_addrlen;
        break;
    }
    if (fd < 0) {
        fperror(output, "socket/connect");
        return 1;
    }

    switch (ai->ai_family) {
        case AF_INET6:
            overhead = 48;
            if (!mtu)
                mtu = 128000;
            if (mtu <= overhead)
                goto pktlen_error;

            on = IPV6_PMTUDISC_DO;
            if (setsockopt(fd, SOL_IPV6, IPV6_MTU_DISCOVER, &on, sizeof(on)) &&
                (on = IPV6_PMTUDISC_DO,
                        setsockopt(fd, SOL_IPV6, IPV6_MTU_DISCOVER, &on, sizeof(on)))) {
                fperror(output, "IPV6_MTU_DISCOVER");
                return 1;
            }
            on = 1;
            if (setsockopt(fd, SOL_IPV6, IPV6_RECVERR, &on, sizeof(on))) {
                fperror(output, "IPV6_RECVERR");
                return 1;
            }
            if (
#ifdef IPV6_RECVHOPLIMIT
setsockopt(fd, SOL_IPV6, IPV6_HOPLIMIT, &on, sizeof(on)) &&
setsockopt(fd, SOL_IPV6, IPV6_2292HOPLIMIT, &on, sizeof(on))
#else
                setsockopt(fd, SOL_IPV6, IPV6_HOPLIMIT, &on, sizeof(on))
#endif
                    ) {
                fperror(output, "IPV6_HOPLIMIT");
                return 1;
            }
            if (!IN6_IS_ADDR_V4MAPPED(&(((struct sockaddr_in6 *) &target)->sin6_addr)))
                break;
            mapped = 1;
            /*FALLTHROUGH*/
        case AF_INET:
            overhead = 28;
            if (!mtu)
                mtu = 65535;
            if (mtu <= overhead)
                goto pktlen_error;

            on = IP_PMTUDISC_DO;
            if (setsockopt(fd, SOL_IP, IP_MTU_DISCOVER, &on, sizeof(on))) {
                fperror(output, "IP_MTU_DISCOVER");
                return 1;
            }
            on = 1;
            if (setsockopt(fd, SOL_IP, IP_RECVERR, &on, sizeof(on))) {
                fperror(output, "IP_RECVERR");
                return 1;
            }
            if (setsockopt(fd, SOL_IP, IP_RECVTTL, &on, sizeof(on))) {
                fperror(output, "IP_RECVTTL");
                return 1;
            }
    }

    pktbuf = malloc(mtu);
    if (!pktbuf) {
        fperror(output, "malloc");
        return 1;
    }

    for (ttl = 1; ttl <= max_hops; ttl++) {
        ssize_t res = -1;
        int i;

        on = ttl;
        switch (ai->ai_family) {
            case AF_INET6:
                if (setsockopt(fd, SOL_IPV6, IPV6_UNICAST_HOPS, &on, sizeof(on))) {
                    fperror(output, "IPV6_UNICAST_HOPS");
                    return 1;
                }
                if (!mapped)
                    break;
                /*FALLTHROUGH*/
            case AF_INET:
                if (setsockopt(fd, SOL_IP, IP_TTL, &on, sizeof(on))) {
                    fperror(output, "IP_TTL");
                    return 1;
                }
        }

        restart:
        for (i = 0; i < 3; i++) {
            size_t old_mtu;

            old_mtu = mtu;
            res = probe_ttl(fd, ai, ttl, output);
            if (mtu != old_mtu)
                goto restart;
            if (res == 0)
                goto done;
            if (res > 0)
                break;
        }

        if (res < 0)
            fprintf(output, "%2d:  no reply\n", ttl);
    }
    fprintf(output, "     Too many hops: pmtu %zu\n", mtu);

    done:
    freeaddrinfo(result);

    fprintf(output, "     Resume: pmtu %zu ", mtu);
    if (hops_to >= 0)
        fprintf(output, "hops %d ", hops_to);
    if (hops_from >= 0)
        fprintf(output, "back %d ", hops_from);
    fprintf(output, "\n");
    return 0;

    pktlen_error:
    fprintf(output, "Error: pktlen must be > %d and <= %d\n",
            overhead, INT_MAX);
    return 1;
}