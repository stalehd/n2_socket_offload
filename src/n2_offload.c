/*
 * Copyright (c) 2018 Foundries.io
 *
 * SPDX-License-Identifier: Apache-2.0
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

// The maximum number of sockets in SARA N2 is 7
#define MDM_MAX_SOCKETS 7

// 2 second timeout for commands. This is ample time<
#define MDM_CMD_TIMEOUT 2

#define CONFIG_N2_NAME "SARA_N2"
#define CONFIG_N2_INIT_PRIORITY 80
#define CONFIG_N2_MAX_PACKET_SIZE 512

struct n2_socket
{
    int id;
    bool connected;
    struct sockaddr *addr;
    int local_port;
    uint8_t *incoming;
    ssize_t incoming_len;
    struct sockaddr *remote_addr;
    ssize_t remote_len;
};

int create_socket_on_modem(int port)
{
    return -1;
}

static struct n2_socket sockets[MDM_MAX_SOCKETS];
static int next_free_socket = 0;
static int next_free_port = 0;

static bool is_valid_socket(int sock_fd)
{
    if (sockets[sock_fd].id)
    {
        return true;
    }
    return false;
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

    if (next_free_socket > MDM_MAX_SOCKETS)
    {
        return -ENOMEM;
    }
    int fd = next_free_socket;
    next_free_socket++;
    sockets[fd].id = next_free_socket;
    sockets[fd].local_port = next_free_port;
    next_free_port++;

    LOG_DBG("Send command: AT+NSOCR=\"DGRAM\",17,%d,1", sockets[fd].local_port);
    // TODO: Implement new socket here. The socket might be initialized but not
    // created at this point since we want to bind to a certain port locally.
    // If sendto is called we'll create the socket.
    return fd;
}

static int offload_close(int sock_fd)
{
    if (!is_valid_socket(sock_fd))
    {
        return -EINVAL;
    }
    sockets[sock_fd].id = 0;
    // use AT+NSOCL
    LOG_DBG("Send command: AT+NSOCL=%d\r\n", sockets[sock_fd].id);
    // Find socket (use index) and close it on the modem
    return 0;
}

static int offload_connect(int sock_fd, const struct sockaddr *addr,
                           socklen_t addrlen)
{
    if (!is_valid_socket(sock_fd))
    {
        return -EINVAL;
    }
    // Find matching socket, then check if it created on the modem. It shouldn't be created
    if (sockets[sock_fd].id != 0)
    {
        return -EISCONN;
    }

    sockets[sock_fd].connected = true;
    sockets[sock_fd].remote_addr = malloc(addrlen);
    memcpy(sockets[sock_fd].remote_addr, addr, addrlen);
    return 0;
}

static int offload_poll(struct pollfd *fds, int nfds, int msecs)
{
    if (nfds != 1)
    {
        return -EINVAL;
    }
    if (!is_valid_socket(fds[0].fd))
    {
        return -EINVAL;
    }
    // Check if there's incoming data from the socket. Only use POLLIN
    return -ENOTSUP;
}

static ssize_t read_incoming_data(int sock_fd, void *buf, short int len)
{
    ssize_t return_len = sockets[sock_fd].incoming_len;
    if (len < sockets[sock_fd].incoming_len)
    {
        return_len = len;
    }

    if (len > sockets[sock_fd].incoming_len)
    {
        return_len = sockets[sock_fd].incoming_len;
    }

    memcpy(buf, sockets[sock_fd].incoming, return_len);

    sockets[sock_fd].incoming_len -= return_len;

    uint8_t *remaining_buf = NULL;
    ssize_t remaining = (sockets[sock_fd].incoming_len - return_len);

    if (remaining > 0)
    {
        remaining_buf = malloc(remaining);
        memcpy(remaining_buf, sockets[sock_fd].incoming + len, remaining);
    }
    free(sockets[sock_fd].incoming);
    sockets[sock_fd].incoming = remaining_buf;
    sockets[sock_fd].incoming_len = remaining;

    if (sockets[sock_fd].incoming_len == 0)
    {
        // clear buffers
        free(sockets[sock_fd].remote_addr);
        sockets[sock_fd].remote_addr = NULL;
        sockets[sock_fd].remote_len = 0;
    }
    return return_len;
}

static ssize_t offload_recvfrom(int sock_fd, void *buf, short int len,
                                short int flags, struct sockaddr *from,
                                socklen_t *fromlen)
{
    ARG_UNUSED(flags);

    if (!is_valid_socket(sock_fd))
    {
        return -EINVAL;
    }
    if (!sockets[sock_fd].incoming)
    {
        // no data waiting
        return 0;
    }
    if (*fromlen < sockets[sock_fd].remote_len)
    {
        // not enough room for socket address
        return -EINVAL;
    }
    memcpy(from, sockets[sock_fd].remote_addr, sockets[sock_fd].remote_len);
    *fromlen = sockets[sock_fd].remote_len;

    return read_incoming_data(sock_fd, buf, len);
}

static ssize_t offload_recv(int sock_fd, void *buf, size_t max_len, int flags)
{
    ARG_UNUSED(flags);

    if (!is_valid_socket(sock_fd))
    {
        return -EINVAL;
    }

    if (!sockets[sock_fd].incoming)
    {
        // no data waiting
        return 0;
    }

    return read_incoming_data(sock_fd, buf, max_len);
}

static ssize_t offload_sendto(int sock_fd, const void *buf, size_t len,
                              int flags, const struct sockaddr *to,
                              socklen_t tolen)
{
    if (!is_valid_socket(sock_fd))
    {
        LOG_DBG("invalid socket fd. Can't sendto()");
        return -EINVAL;
    }

    if (len > CONFIG_N2_MAX_PACKET_SIZE)
    {
        LOG_DBG("Too long packet (%d). Can't sendto()", len);
        return -EINVAL;
    }

    LOG_DBG("Send command: AT+NSOST=%d,[addr],[port],[len],[hex]", sockets[sock_fd].id);
    return len;
}

static ssize_t offload_send(int sock_fd, const void *buf, size_t len, int flags)
{
    if (!is_valid_socket(sock_fd))
    {
        LOG_DBG("Invalid socket. Can't send()");
        return -EINVAL;
    }
    if (!sockets[sock_fd].connected)
    {
        return -ENOTCONN;
    }

    LOG_DBG("Send command: AT+NSOST=%d,[addr],[port],[len],[hex]", sockets[sock_fd].id);
    return len;
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
    iface->if_dev->offload = &offload_funcs;
    socket_offload_register(&n2_socket_offload);
}

static struct net_if_api api_funcs = {
    .init = offload_iface_init,
};

// _init initializes the network offloading
static int n2_init(struct device *dev)
{
    ARG_UNUSED(dev);
    // TODO: Set up the threads et al here.
    LOG_DBG("Send command: AT+CFUN=0");
    LOG_DBG("Send command: AT+CGDCONT=0,\"IP\",\"mda.ee\"");
    LOG_DBG("Send command: AT+CFUN=1");
    LOG_DBG("Wait for IP address");
    k_sleep(3000);
    LOG_DBG("n2_init completed");
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
