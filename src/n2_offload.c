/*
 * Copyright (c) 2018 Foundries.io
 *
ß * SPDX-License-Identifier: Apache-2.0
 */

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(sara_n2);
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
static int next_free_socket = 0;
static int next_free_port = 6000;

#define CMD_BUFFER_SIZE 32
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
    LOG_INF("next= %d", next_free_socket);
    if (!VALID_SOCKET(sock_fd))
    {
        return -EINVAL;
    }
    k_sem_take(&mdm_sem, K_FOREVER);
    sprintf(modem_command_buffer, "AT+NSOCL=%d\r\n", sockets[sock_fd].id);
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
    LOG_INF("next= %d", next_free_socket);
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
        LOG_ERR("poll has invalid nfds: %d", nfds);
        return -EINVAL;
    }
    k_sem_take(&mdm_sem, K_FOREVER);
    at_poll(10);
    for (int i = 0; i < nfds; i++)
    {
        if (!VALID_SOCKET(fds[i].fd))
        {
            fds[i].revents = POLLNVAL;
            continue;
        }
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
    LOG_INF("next= %d", next_free_socket);
    ARG_UNUSED(flags);
    if (!VALID_SOCKET(sock_fd))
    {
        LOG_ERR("Invalid socket fd: %d", sock_fd);
        return -EINVAL;
    }

    k_sem_take(&mdm_sem, K_FOREVER);

    // Now here's an interesting bit of information: If you send AT+NSORF *before*
    // you receive the +NSONMI URC from the module you'll get just three fields
    // in return: socket, data, remaining. IT WOULD HAVE BEEN REALLY NICE IF THE
    // DOCUMENTATION INCLUDED THIS.
    if (sockets[sock_fd].incoming_len == 0)
    {
        LOG_INF("Popping the cherry");
        modem_write("AT\r\n");
        at_decode();
    }

    // Use NSORF to read incoming data.
    sprintf(modem_command_buffer, "AT+NSORF=%d,%d\r\n", sockets[sock_fd].id, len);
    modem_write(modem_command_buffer);

    char ip[16];
    int port = 0;
    size_t remain = 0;
    int sockfd = 0;
    size_t received = 0;
    if (atnsorf_decode(&sockfd, ip, &port, buf, &received, &remain) == AT_OK)
    {
        if (received == 0)
        {
            k_sem_give(&mdm_sem);
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
        LOG_INF("recv(fd=%d, len=%d, remain=%d)", sock_fd, len, remain);
        k_sem_give(&mdm_sem);
        return received;
    }
    k_sem_give(&mdm_sem);
    return -ENOMEM;
}

static int offload_recv(int sock_fd, void *buf, size_t max_len, int flags)
{
    LOG_INF("next= %d", next_free_socket);
    ARG_UNUSED(flags);
    LOG_INF("recv()");

    if (!VALID_SOCKET(sock_fd))
    {
        LOG_ERR("Invalid socket fd: %d", sock_fd);
        return -EINVAL;
    }
    k_sem_take(&mdm_sem, K_FOREVER);
    if (!sockets[sock_fd].connected)
    {
        k_sem_give(&mdm_sem);
        return -EINVAL;
    }

    while (sockets[sock_fd].incoming_len == 0)
    {
        // We'll need to poll for URC
        at_poll(500);
    }
    LOG_INF("recv() call recvfrom(max_len=%d)", max_len);
    k_sem_give(&mdm_sem);
    return offload_recvfrom(sock_fd, buf, max_len, flags, NULL, NULL);
}

static int offload_sendto(int sock_fd, const void *buf, size_t len,
                          int flags, const struct sockaddr *to,
                          socklen_t tolen)
{
    LOG_INF("next= %d", next_free_socket);
    if (!VALID_SOCKET(sock_fd))
    {
        LOG_ERR("Invalid socket fd: %d", sock_fd);
        return -EINVAL;
    }

    if (len > CONFIG_N2_MAX_PACKET_SIZE)
    {
        LOG_ERR("Too long packet (%d). Can't sendto()", len);
        return -EINVAL;
    }

    k_sem_take(&mdm_sem, K_FOREVER);
    struct sockaddr_in *toaddr = (struct sockaddr_in *)to;

    char addr[64];
    if (!inet_ntop(AF_INET, &toaddr->sin_addr, addr, 128))
    {
        LOG_ERR("Unable to convert address to string");
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

    modem_write("\"\r\n");

    int written = len;
    switch (atnsost_decode())
    {
    case AT_OK:
        break;
    case AT_ERROR:
        LOG_ERR("ERROR response");
        written = -ENOMEM;
        break;
    case AT_TIMEOUT:
        LOG_ERR("Timeout reading response from AT+NSOST");
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
        LOG_ERR("Invalid socket fd: %d", sock_fd);
        return -EINVAL;
    }
    k_sem_take(&mdm_sem, K_FOREVER);

    if (!sockets[sock_fd].connected)
    {
        LOG_ERR("Socket not connected: (%d)", sock_fd);
        k_sem_give(&mdm_sem);
        return -ENOTCONN;
    }
    LOG_INF("send(fd=%d, len=%i)", sock_fd, len);
    k_sem_give(&mdm_sem);
    int ret = offload_sendto(sock_fd, buf, len, flags,
                             sockets[sock_fd].remote_addr, sockets[sock_fd].remote_len);
    LOG_INF("send(fd=%d, len=%i) completed (ret=%d)", sock_fd, len, ret);
    return ret;
}

static int offload_socket(int family, int type, int proto)
{
    if (family != AF_INET)
    {
        LOG_ERR("Unsupported family (%d)", family);
        return -EAFNOSUPPORT;
    }
    if (type != SOCK_DGRAM)
    {
        LOG_ERR("Unsupported type (%d)", type);
        return -ENOTSUP;
    }
    if (proto != IPPROTO_UDP)
    {
        LOG_ERR("Unsupported proto (%d)", proto);
        return -ENOTSUP;
    }

    if (next_free_socket > MDM_MAX_SOCKETS)
    {
        LOG_ERR("Max sockets in use (next free=%d)", next_free_socket);
        return -ENOMEM;
    }
    k_sem_take(&mdm_sem, K_FOREVER);
    int fd = next_free_socket;
    next_free_socket++;
    sockets[fd].local_port = next_free_port;
    next_free_port++;

    sprintf(modem_command_buffer, "AT+NSOCR=\"DGRAM\",17,%d,1\r\n", sockets[fd].local_port);
    modem_write(modem_command_buffer);

    int sockfd = -1;
    if (atnsocr_decode(&sockfd) == AT_OK)
    {
        sockets[fd].id = sockfd;
        LOG_INF("socket() end: Created socket. fd = %d, modem fd = %d, local port = %d, next= %d", fd, sockets[fd].id, sockets[fd].local_port, next_free_socket);
        k_sem_give(&mdm_sem);
        return fd;
    }
    LOG_ERR("Unable to decode NSOCR");
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
    LOG_DBG("offload_iface_init");
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
    LOG_INF("Received %d bytes from socket %d", bytes, fd);
    for (int i = 0; i < MDM_MAX_SOCKETS; i++)
    {
        if (sockets[i].id == fd)
        {
            sockets[i].incoming_len += bytes;
        }
    }
}
// _init initializes the network offloading
static int n2_init(struct device *dev)
{
    ARG_UNUSED(dev);

    k_sem_init(&mdm_sem, 1, 1);

    receive_callback(receive_cb);

    modem_init();
    modem_restart();
    LOG_INF("Waiting for modem to connect...");
    while (!modem_is_ready())
    {
        k_sleep(K_MSEC(2000));
    }
    LOG_INF("Modem is ready. Turning off PSM");
    modem_write("AT+CPSMS=0\r\n");
    if (atcpsms_decode() != AT_OK)
    {
        LOG_ERR("Unable to turn off PSM for modem");
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
