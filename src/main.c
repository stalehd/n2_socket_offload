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
    init_comms();

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

    LOG_DBG("Enter send loop");

    modem_write("AT+CFUN=0\r\n");
    k_sleep(K_MSEC(1500));
    modem_write("AT+CGDCONT=0,\"IP\",\"mda.ee\"\r\n");
    k_sleep(K_MSEC(1500));
    modem_write("AT+NCONFIG=\"AUTOCONNECT\",\"TRUE\"\r\n");
    k_sleep(K_MSEC(1500));
    modem_write("AT+CFUN=1\r\n");
    k_sleep(K_MSEC(1500));
    modem_write("AT+CIMI\r\n");
    k_sleep(K_MSEC(1500));
    modem_write("AT+CGSN=1\r\n");
    k_sleep(K_MSEC(1500));
    modem_write("AT+NCONFIG?\r\n");
    k_sleep(K_MSEC(2500));
    modem_write("AT+CGDCONT?\r\n");
    k_sleep(K_MSEC(1500));
    while (true)
    {
        k_sleep(K_MSEC(5000));
        modem_write("AT+CGPADDR\r\n");
    }

    LOG_DBG("Halting firmware");
}