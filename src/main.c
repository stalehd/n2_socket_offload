/*
    Copyright 2020 Telenor Digital AS

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "config.h"

#include <logging/log.h>
#define LOG_LEVEL APP_LOG_LEVEL
LOG_MODULE_REGISTER(app);

#include <zephyr.h>
#include <net/socket.h>

#include "comms.h"

static const char *message = "Hello there";

void main(void)
{
    modem_init();

    modem_restart();

    while (!modem_is_ready())
    {
        LOG_INF("Waiting for modem...");
        k_sleep(K_MSEC(2000));
    }

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

    LOG_DBG("Sent message, waiting for downstream");
    bool received = false;
    while (!received)
    {
// Wait for response
#define BUF_LEN 128
        u8_t buffer[BUF_LEN];
        memset(buffer, 0, BUF_LEN);
        struct sockaddr_in *addr;
        socklen_t addrlen;
        int err = recvfrom(sock, buffer, BUF_LEN, 0, (struct sockaddr *)&addr, &addrlen);
        if (err < 0)
        {
            LOG_ERR("Error receiving: %d", err);
            break;
        }
        if (err > 0)
        {
            LOG_DBG("Got data (%d bytes): %s", err, log_strdup(buffer));
            received = true;
        }
        k_sleep(K_MSEC(1000));
    }
    close(sock);

    // OK - great success. Now use CoAP to POST to the backend.

    LOG_DBG("Halting firmware");
}