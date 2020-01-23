/*
 * Copyright (c) 2018 Foundries.io
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdbool.h>
#include <zephyr/types.h>
#include <errno.h>
#include <zephyr.h>
#include <device.h>
#include <init.h>
#include <net/net_offload.h>
#include <net/socket_offload.h>
#include <stdio.h>

#include "config.h"
#include "comms.h"
#include "at_commands.h"

// The maximum number of sockets in SARA N2 is 7
#define MDM_MAX_SOCKETS 7
#define INVALID_FD -1

struct n2_socket
{
    int id;
    bool connected;
    //    struct sockaddr *addr;
    int local_port;
    ssize_t incoming_len;
    void *remote_addr;
    ssize_t remote_len;
};

static struct n2_socket sockets[MDM_MAX_SOCKETS];
static int next_free_port = 6000;

#define CMD_BUFFER_SIZE 64
static char modem_command_buffer[CMD_BUFFER_SIZE];

#define CMD_TIMEOUT 2000

#define TO_HEX(i) (i <= 9 ? '0' + i : 'A' - 10 + i)
#define VALID_SOCKET(s) (sockets[s].id >= 0)

static struct k_sem mdm_sem;

/**
 * @brief Clear socket state
 */
static void clear_socket(int sock_fd)
{
    sockets[sock_fd].id = -1;
    sockets[sock_fd].connected = false;
    sockets[sock_fd].local_port = 0;
    sockets[sock_fd].incoming_len = 0;
    sockets[sock_fd].remote_len = 0;
    if (sockets[sock_fd].remote_addr != NULL)
    {
        k_free(sockets[sock_fd].remote_addr);
    }
}

static int offload_close(int sock_fd)
{
    if (!VALID_SOCKET(sock_fd))
    {
        return -EINVAL;
    }
    k_sem_take(&mdm_sem, K_FOREVER);
    sprintf(modem_command_buffer, "AT+NSOCL=%d\r", sockets[sock_fd].id);
    modem_write(modem_command_buffer);

    if (atnsocl_decode() != AT_OK)
    {
        k_sem_give(&mdm_sem);
        return -ENOMEM;
    }
    clear_socket(sock_fd);
    k_sem_give(&mdm_sem);
    return 0;
}

static int offload_connect(int sock_fd, const struct sockaddr *addr,
                           socklen_t addrlen)
{
    if (!VALID_SOCKET(sock_fd))
    {
        return -EINVAL;
    }
    k_sem_take(&mdm_sem, K_FOREVER);
    // Find matching socket, then check if it created on the modem. It shouldn't be created
    if (sockets[sock_fd].id != 0)
    {
        k_sem_give(&mdm_sem);
        return -EISCONN;
    }

    sockets[sock_fd].connected = true;
    sockets[sock_fd].remote_addr = k_malloc(addrlen);
    memcpy(sockets[sock_fd].remote_addr, addr, addrlen);
    k_sem_give(&mdm_sem);
    return 0;
}

static int offload_poll(struct pollfd *fds, int nfds, int msecs)
{
    if (nfds != 1)
    {
        printf("poll has invalid nfds: %d\n", nfds);
        return -EINVAL;
    }
    // A small breather to make sure poll() doesn't hog the CPU
    k_sleep(100);
    k_sem_take(&mdm_sem, K_FOREVER);
    for (int i = 0; i < nfds; i++)
    {
        if (!VALID_SOCKET(fds[i].fd))
        {
            fds[i].revents = POLLNVAL;
            continue;
        }
        fds[i].revents = POLLOUT;
        if (sockets[fds[i].fd].incoming_len > 0)
        {
            fds[i].revents |= POLLIN;
        }
    }
    k_sem_give(&mdm_sem);
    return 0;
}

static int offload_recvfrom(int sock_fd, void *buf, short int len,
                            short int flags, struct sockaddr *from,
                            socklen_t *fromlen)
{
    ARG_UNUSED(flags);
    if (!VALID_SOCKET(sock_fd))
    {
        printf("Invalid socket fd: %d\n", sock_fd);
        return -EINVAL;
    }

    k_sem_take(&mdm_sem, K_FOREVER);

    // Now here's an interesting bit of information: If you send AT+NSORF *before*
    // you receive the +NSONMI URC from the module you'll get just three fields
    // in return: socket, data, remaining. IT WOULD HAVE BEEN REALLY NICE IF THE
    // DOCUMENTATION INCLUDED THIS.

    if (sockets[sock_fd].incoming_len == 0)
    {
        k_sem_give(&mdm_sem);
        printf("Socket %d has no data waiting, returning 0/EWOULDBLOCK\n", sock_fd);
        errno = EWOULDBLOCK;
        return 0;
    }

    // Use NSORF to read incoming data.
    memset(modem_command_buffer, 0, sizeof(modem_command_buffer));
    sprintf(modem_command_buffer, "AT+NSORF=%d,%d\r", sockets[sock_fd].id, len);
    printf("Sending: %s\n", modem_command_buffer);
    modem_write(modem_command_buffer);

    char ip[16];
    int port = 0;
    size_t remain = 0;
    int sockfd = 0;
    size_t received = 0;

    int res = atnsorf_decode(&sockfd, ip, &port, buf, &received, &remain);
    if (res == AT_OK)
    {
        printf("decode data (fd=%d bytes=%d, remain=%d)\n", sock_fd, received, remain);
        if (received == 0)
        {
            k_sem_give(&mdm_sem);
            printf("Received 0 bytes from nsorf (fd=%d)\n", sock_fd);
            return 0;
        }
        if (fromlen != NULL)
        {
            *fromlen = sizeof(struct sockaddr_in);
        }
        if (from != NULL)
        {
            ((struct sockaddr_in *)from)->sin_family = AF_INET;
            ((struct sockaddr_in *)from)->sin_port = htons(port);
            inet_pton(AF_INET, ip, &((struct sockaddr_in *)from)->sin_addr);
        }
        sockets[sock_fd].incoming_len = remain;
        k_sem_give(&mdm_sem);
        printf("recv() got %d bytes from fd=%d (%d remaining)\n", received, sock_fd, remain);
        return received;
    }
    k_sem_give(&mdm_sem);
    printf("recvfrom(): Got %d when decoding NSORF for %d\n", res, sock_fd);
    errno = -ENOMEM;
    return -ENOMEM;
}

static int offload_recv(int sock_fd, void *buf, size_t max_len, int flags)
{
    ARG_UNUSED(flags);

    if (!VALID_SOCKET(sock_fd))
    {
        printf("Invalid socket fd: %d\n", sock_fd);
        return -EINVAL;
    }
    k_sem_take(&mdm_sem, K_FOREVER);
    if (!sockets[sock_fd].connected)
    {
        k_sem_give(&mdm_sem);
        printf("Socket isn't connected (fd=%d)\n", sock_fd);
        return -EINVAL;
    }

    if (sockets[sock_fd].incoming_len == 0 && ((flags & MSG_DONTWAIT) == MSG_DONTWAIT))
    {
        k_sem_give(&mdm_sem);
        errno = EWOULDBLOCK;
        return 0;
    }

    int curcount = sockets[sock_fd].incoming_len;
    k_sem_give(&mdm_sem);

    while (curcount == 0)
    {
        // busy wait for data
        k_sleep(1000);
        k_sem_take(&mdm_sem, K_FOREVER);
        curcount = sockets[sock_fd].incoming_len;
        if (curcount > 0) {
            printf("Got data while waiting. Great success!\n");
        }
        k_sem_give(&mdm_sem);
    }
    return offload_recvfrom(sock_fd, buf, max_len, flags, NULL, NULL);
}

static int offload_sendto(int sock_fd, const void *buf, size_t len,
                          int flags, const struct sockaddr *to,
                          socklen_t tolen)
{
    if (!VALID_SOCKET(sock_fd))
    {
        printf("Invalid socket fd: %d\n", sock_fd);
        return -EINVAL;
    }

    if (len > CONFIG_N2_MAX_PACKET_SIZE)
    {
        printf("Too long packet (%d). Can't sendto()\n", len);
        return -EINVAL;
    }

    k_sem_take(&mdm_sem, K_FOREVER);

    struct sockaddr_in *toaddr = (struct sockaddr_in *)to;

    char addr[64];
    if (!inet_ntop(AF_INET, &toaddr->sin_addr, addr, 128))
    {
        printf("Unable to convert address to string\n");
        // couldn't read address. Bail out
        k_sem_give(&mdm_sem);
        return -EINVAL;
    }

    sprintf(modem_command_buffer,
            "AT+NSOST=%d,\"%s\",%d,%d,\"",
            sockets[sock_fd].id, addr,
            ntohs(toaddr->sin_port),
            len);

    modem_write(modem_command_buffer);

    char byte[3];
    for (int i = 0; i < len; i++)
    {
        byte[0] = TO_HEX((((const char *)buf)[i] >> 4));
        byte[1] = TO_HEX((((const char *)buf)[i] & 0xF));
        byte[2] = 0;
        modem_write(byte);
    }

    modem_write("\"\r");

    int written = len;
    int fd = -1;
    size_t sent = 0;
    switch (atnsost_decode(&fd, &sent))
    {
    case AT_OK:
        printf("Sucessfully sent %d bytes on fd=%d\n", sent, fd);
        break;
    case AT_ERROR:
        printf("ERROR response from NSOST\n");
        written = -ENOMEM;
        break;
    case AT_TIMEOUT:
        printf("Timeout reading response from AT+NSOST\n");
        written = -ENOMEM;
        break;
    }
    k_sem_give(&mdm_sem);

    return written;
}

static int offload_send(int sock_fd, const void *buf, size_t len, int flags)
{
    if (!VALID_SOCKET(sock_fd))
    {
        printf("Invalid socket fd: %d\n", sock_fd);
        return -EINVAL;
    }
    k_sem_take(&mdm_sem, K_FOREVER);

    if (!sockets[sock_fd].connected)
    {
        printf("Socket not connected: (%d)\n", sock_fd);
        k_sem_give(&mdm_sem);
        return -ENOTCONN;
    }
    k_sem_give(&mdm_sem);
    int ret = offload_sendto(sock_fd, buf, len, flags,
                             sockets[sock_fd].remote_addr, sockets[sock_fd].remote_len);
    return ret;
}

static int offload_socket(int family, int type, int proto)
{
    if (family != AF_INET)
    {
        return -EAFNOSUPPORT;
    }
    if (type != SOCK_DGRAM)
    {
        return -ENOTSUP;
    }
    if (proto != IPPROTO_UDP)
    {
        return -ENOTSUP;
    }

    k_sem_take(&mdm_sem, K_FOREVER);
    int fd = INVALID_FD;
    for (uint8_t i = 0; i < MDM_MAX_SOCKETS; i++) {
        if (sockets[i].id == INVALID_FD) {
            fd = i;
            break;
        }
    }
    if (fd == INVALID_FD) {
        printf("socket(): No free sockets\n");
        return -ENOMEM;
    }
    sockets[fd].local_port = next_free_port;
    next_free_port++;

    sprintf(modem_command_buffer, "AT+NSOCR=\"DGRAM\",17,%d,1\r", sockets[fd].local_port);
    modem_write(modem_command_buffer);

    int sockfd = -1;
    if (atnsocr_decode(&sockfd) == AT_OK)
    {
        sockets[fd].id = sockfd;
        printf("socket(): created fd = %d, modem fd = %d, local port = %d\n", fd, sockets[fd].id, sockets[fd].local_port);
        k_sem_give(&mdm_sem);
        return fd;
    }
    printf("Unable to decode NSOCR\n");
    k_sem_give(&mdm_sem);
    return -ENOMEM;
}

// We're only interested in socket(), close(), connect(), poll()/POLLIN, send() and recvfrom()
// since that's what the lwm2m client/coap library uses.
// bind(), accept(), fctl(), freeaddrinfo(), getaddrinfo(), setsockopt(),
// getsockopt() and listen() is not implemented
static const struct socket_offload n2_socket_offload = {
    .socket = offload_socket,
    .close = offload_close,
    .connect = offload_connect,
    .poll = offload_poll,
    .recv = offload_recv,
    .recvfrom = offload_recvfrom,
    .send = offload_send,
    .sendto = offload_sendto,
};

static int dummy_offload_get(sa_family_t family,
                             enum net_sock_type type,
                             enum net_ip_protocol ip_proto,
                             struct net_context **context)
{
    return -ENOTSUP;
}

// Zephyr doesn't like a null offload so we'll use a dummy offload here.
static struct net_offload offload_funcs = {
    .get = dummy_offload_get,
};

// Offload the interface. This will set the dummy offload functions then
// the socket offloading.
static void offload_iface_init(struct net_if *iface)
{
    for (int i = 0; i < MDM_MAX_SOCKETS; i++)
    {
        sockets[i].id = -1;
        sockets[i].remote_addr = NULL;
    }
    iface->if_dev->offload = &offload_funcs;
    socket_offload_register(&n2_socket_offload);
}

static struct net_if_api api_funcs = {
    .init = offload_iface_init,
};

static void receive_cb(int fd, size_t bytes)
{
    printf("Callback for receive: fd=%d, bytes=%d\n", fd, bytes);
    k_sem_take(&mdm_sem, K_FOREVER);
    for (int i = 0; i < MDM_MAX_SOCKETS; i++)
    {
        if (sockets[i].id == fd)
        {
            printf("Received %d bytes from socket %d\n", bytes, fd);
            sockets[i].incoming_len += bytes;
        }
    }
    k_sem_give(&mdm_sem);
    printf("Callback for receive completed (fd=%d, bytes=%d)\n", fd, bytes);
}

// _init initializes the network offloading
static int n2_init(struct device *dev)
{
    ARG_UNUSED(dev);

    k_sem_init(&mdm_sem, 1, 1);

    receive_callback(receive_cb);

    modem_init();

    modem_write("AT+NSOCL=0\r");
    at_decode();
    modem_write("AT+NSOCL=1\r");
    at_decode();
    modem_write("AT+NSOCL=2\r");
    at_decode();
    modem_write("AT+NSOCL=3\r");
    at_decode();
    modem_write("AT+NSOCL=4\r");
    at_decode();
    modem_write("AT+NSOCL=5\r");
    at_decode();
    modem_write("AT+NSOCL=6\r");
    at_decode();

    //modem_restart();


    printf("Waiting for modem to connect...\n");
    while (!modem_is_ready())
    {
        k_sleep(K_MSEC(2000));
    }
    modem_write("AT+CIMI\r");
    char imsi[24];
    if (atcimi_decode((char *)&imsi) != AT_OK)
    {
        printf("Unable to retrieve IMSI from modem\n");
    }
    else
    {
        printf("IMSI for modem is %s\n", log_strdup(imsi));
    }
    printf("Modem is ready. Turning off PSM\n");
    modem_write("AT+CPSMS=0\r");
    if (atcpsms_decode() != AT_OK)
    {
        printf("Unable to turn off PSM for modem\n");
    }
    return 0;
}

struct n2_iface_ctx
{
};

static struct n2_iface_ctx n2_ctx;

NET_DEVICE_OFFLOAD_INIT(sara_n2, CONFIG_N2_NAME,
                        n2_init, &n2_ctx, NULL,
                        CONFIG_N2_INIT_PRIORITY, &api_funcs,
                        CONFIG_N2_MAX_PACKET_SIZE);
