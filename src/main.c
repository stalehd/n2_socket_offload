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
#include "fota.h"

static char *message = "Hello there";
void udpTest()
{
    LOG_DBG("Sending packet (%s)", message);
    int err;

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        LOG_ERR("Error opening socket: %d", sock);
        return;
    }

    LOG_DBG("Connecting");
    static struct sockaddr_in remote_addr = {
        sin_family : AF_INET,
        sin_port : htons(1234),
    };
    net_addr_pton(AF_INET, "172.16.15.14", &remote_addr.sin_addr);

    err = connect(sock, (struct sockaddr *)&remote_addr, sizeof(remote_addr));
    if (err < 0)
    {
        LOG_ERR("Unable to connect to backend: %d", err);
        close(sock);
        return;
    }
    LOG_DBG("Connected, sending message");
    err = send(sock, message, strlen(message), 0);
    if (err < 0)
    {
        LOG_ERR("Error sending: %d", err);
        close(sock);
        return;
    }

    LOG_DBG("Waiting for downstream message");
    bool received = false;
    /*
    while (!received)
    {
// Wait for response
#define BUF_LEN 64
        u8_t buffer[BUF_LEN];
        memset(buffer, 0, BUF_LEN);
        int err = recv(sock, buffer, BUF_LEN, 0);
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
    }*/
    close(sock);

    // OK - great success. Now use CoAP to POST to the backend.

    LOG_DBG("Halting firmware");
}

void fotaTest()
{
    // Initialize the application and run any self-tests before calling fota_init.
    // Otherwise, if initialization or self-tests fail after an update, reboot the system and the previous firmware image will be used.

    int ret = fota_init();
    if (ret)
    {
        LOG_ERR("fota_init: %d", ret);
        return;
    }
}

void main(void)
{

    k_sleep(10000);

    udpTest();
}