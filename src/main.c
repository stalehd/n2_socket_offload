#include "config.h"

#include <logging/log.h>
#define LOG_LEVEL APP_LOG_LEVEL
LOG_MODULE_REGISTER(app);

#include <zephyr.h>
#include <net/socket.h>

static const char *message = "Hello there";

void main(void)
{
    LOG_DBG("Sending packet (%s)", message);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        LOG_ERR("Error opening socket: %d", sock);
        return;
    }

    LOG_DBG("Preparing packet");

    static struct sockaddr_in remote_addr = {
        sin_family : AF_INET,
        sin_port : htons(1234),
    };
    net_addr_pton(AF_INET, "172.16.15.14", &remote_addr.sin_addr);

    int err = sendto(sock, message, strlen(message), 0, (struct sockaddr *)&remote_addr, sizeof(remote_addr));
    if (err < 0)
    {
        LOG_ERR("Error sending: %d", err);
        close(sock);
        return;
    }

    close(sock);

    LOG_DBG("Packet sent");
}