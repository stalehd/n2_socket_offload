#include "config.h"
#include <zephyr.h>
#include <stdio.h>
#include <net/socket.h>
#include "test_udp.h"

void testUDPCounter()
{
    printf("Counting bytes\n");
    int err;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        printf("Error opening socket: %d\n", sock);
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
        printf("Unable to connect to backend: %d\n", err);
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

        printf("Sending %d bytes\n", len);
        err = send(sock, dump, len, 0);
        if (err < len)
        {
            printf("Error sending (%d bytes sent): %d\n", len, err);
            close(sock);
            return;
        }

        k_sleep(2000);
    }
    close(sock);
}


#define UDP_MESSAGE "Hello there I'm the UDP message 7"

void testUDP()
{
    int err = 0;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        printf("Error opening socket: %d\n", sock);
        return;
    }

    printf("Connecting\n");
    static struct sockaddr_in remote_addr = {
        sin_family : AF_INET,
    };
    remote_addr.sin_port = htons(1234);

    net_addr_pton(AF_INET, "172.16.15.14", &remote_addr.sin_addr);

    err = connect(sock, (struct sockaddr *)&remote_addr, sizeof(remote_addr));
    if (err < 0)
    {
        printf("Unable to connect to backend: %d\n", err);
        close(sock);
        return;
    }

    printf("Sending a single message\n");
    err = send(sock, UDP_MESSAGE, strlen(UDP_MESSAGE), 0);
    if (err < strlen(UDP_MESSAGE))
    {
        printf("Error sending: %d\n", err);
        close(sock);
        return;
    }

    printf("Wait for packet\n");

#define BUF_LEN 12
    u8_t buffer[BUF_LEN + 1];
    memset(buffer, 0, BUF_LEN);

    // receive first block and block
    err = recv(sock, buffer, BUF_LEN, 0);
    if (err <= 0)
    {
        printf("Error receiving: %d\n", err);
        close(sock);
        return;
    }
    buffer[err] = 0;
    printf("Received %d bytes (%s)\n", err, log_strdup(buffer));

    while (err > 0)
    {
        memset(buffer, 0, BUF_LEN);
        err = recv(sock, buffer, BUF_LEN, MSG_DONTWAIT);
        if (err > 0)
        {
            buffer[err] = 0;
            printf("Received %d bytes (%s)\n", err, log_strdup(buffer));
        }
        if (err < 0 && err != -EAGAIN) {
            printf("Error receiving: %d\n", err);
            close(sock);
            return;
        }
    }

    printf("Connected, sending final message...\n");
    err = send(sock, UDP_MESSAGE, strlen(UDP_MESSAGE), 0);
    if (err < strlen(UDP_MESSAGE))
    {
        printf("Error sending: %d\n", err);
        close(sock);
        return;
    }

    close(sock);
    printf("Done socket\n");
}