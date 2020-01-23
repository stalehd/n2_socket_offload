#include "config.h"
#include <logging/log.h>
#define LOG_LEVEL APP_LOG_LEVEL
LOG_MODULE_REGISTER(udp_test);

#include <zephyr.h>
#include <stdio.h>
#include <net/socket.h>
#include "test_udp.h"

void testUDPCounter()
{
    LOG_DBG("Counting bytes");
    int err;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        LOG_ERR("Error opening socket: %d", sock);
        return;
    }

    static struct sockaddr_in remote_addr = {
        sin_family : AF_INET,
    };
    remote_addr.sin_port = htons(1234);

    net_addr_pton(AF_INET, "172.16.15.14", &remote_addr.sin_addr);

    err = connect(sock, (struct sockaddr *)&remote_addr, sizeof(remote_addr));
    if (err < 0)
    {
        LOG_ERR("Unable to connect to backend: %d", err);
        close(sock);
        return;
    }

    char buffer[255];
    char dump[512];
    memset(dump, 0, sizeof(dump));
    memset(buffer, 0, sizeof(buffer));
    for (int i = 0; i < sizeof(buffer); i++) {
        buffer[i] = '.';
        int len = 21 + strlen(buffer);
        sprintf(dump, "%03d bytes on the wall%s", len, buffer);

        LOG_INF("Sending %d bytes", len);
        err = send(sock, dump, len, 0);
        if (err < len)
        {
            LOG_ERR("Error sending (%d bytes sent): %d", len, err);
            close(sock);
            return;
        }

        k_sleep(2000);
    }
    close(sock);
}


#define UDP_MESSAGE "Hello there I'm the UDP message 3"

void testUDP()
{
    int err = 0;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        LOG_ERR("Error opening socket: %d", sock);
        return;
    }

    LOG_DBG("Connecting");
    static struct sockaddr_in remote_addr = {
        sin_family : AF_INET,
    };
    remote_addr.sin_port = htons(1234);

    net_addr_pton(AF_INET, "172.16.15.14", &remote_addr.sin_addr);

    err = connect(sock, (struct sockaddr *)&remote_addr, sizeof(remote_addr));
    if (err < 0)
    {
        LOG_ERR("Unable to connect to backend: %d", err);
        close(sock);
        return;
    }

    LOG_DBG("Sending a single message");
    err = send(sock, UDP_MESSAGE, strlen(UDP_MESSAGE), 0);
    if (err < strlen(UDP_MESSAGE))
    {
        LOG_ERR("Error sending: %d", err);
        close(sock);
        return;
    }

    LOG_DBG("Wait for packet");

#define BUF_LEN 12
    u8_t buffer[BUF_LEN + 1];
    memset(buffer, 0, BUF_LEN);

    // receive first block and block
    err = recv(sock, buffer, BUF_LEN, 0);
    if (err <= 0)
    {
        LOG_ERR("Error receiving: %d", err);
        close(sock);
        return;
    }
    buffer[err] = 0;
    LOG_INF("Received %d bytes (%s)", err, log_strdup(buffer));

    while (err > 0)
    {
        memset(buffer, 0, BUF_LEN);
        err = recv(sock, buffer, BUF_LEN, MSG_DONTWAIT);
        if (err > 0)
        {
            buffer[err] = 0;
            LOG_INF("Received %d bytes (%s)", err, log_strdup(buffer));
        }
        if (err < 0 && err != -EAGAIN) {
            LOG_ERR("Error receiving: %d", err);
            close(sock);
            return;
        }
    }

    LOG_DBG("Connected, sending final message...");
    err = send(sock, UDP_MESSAGE, strlen(UDP_MESSAGE), 0);
    if (err < strlen(UDP_MESSAGE))
    {
        LOG_ERR("Error sending: %d", err);
        close(sock);
        return;
    }

    close(sock);
    LOG_INF("Done socket");
}