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

#define MAX_RESP_SIZE 256
static char resp[MAX_RESP_SIZE];

static char buf_copy[256];
static void send_command(const char *buf, s32_t timeout)
{
    size_t len = strlen(buf);
    memcpy(buf_copy, buf, len);
    buf_copy[len - 2] = 0;
    LOG_DBG("Sent: %s", log_strdup(buf_copy));

    modem_write(buf);
    k_sleep(K_MSEC(timeout));

    int read = modem_read(resp, MAX_RESP_SIZE, K_MSEC(timeout));
    if (read == 0)
    {
        LOG_DBG("0 bytes returned");
        return;
    }
    resp[read] = 0;
}

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

    k_sleep(K_MSEC(500));

    // Turn on signalling status
    send_command("AT+CEREG=1\r\n", 1500);

    send_command("AT+NPSMR=1\r\n", 1500);

    send_command("AT+CFUN=0\r\n", 1500);

    send_command("AT+CGDCONT=0,\"IP\",\"mda.ee\"\r\n", 1500);

    send_command("AT+NCONFIG=\"AUTOCONNECT\",\"TRUE\"\r\n", 1500);

    send_command("AT+CFUN=1\r\n", 1500);

    // Attach the terminal
    send_command("AT+CGATT=1\r\n", 5000);

    send_command("AT+CIMI\r\n", 1500);

    send_command("AT+CGSN=1\r\n", 1500);

    send_command("AT+NCONFIG?\r\n", 2500);

    // send_command("AT+CGDCONT?\r\n", 1500);

    while (true)
    {
        k_sleep(K_MSEC(30000));
        send_command("AT+CGPADDR\r\n", 1500);
    }

    LOG_DBG("Halting firmware");
}