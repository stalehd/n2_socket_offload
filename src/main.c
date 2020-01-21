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
#include <stdio.h>
#include <logging/log.h>
#define LOG_LEVEL APP_LOG_LEVEL
LOG_MODULE_REGISTER(app);

#include <zephyr.h>
#include <net/socket.h>
#include <net/coap.h>

#include "comms.h"
#include "fota.h"

#define UDP_MESSAGE "Hello there II"
void udpTest()
{
    LOG_DBG("Sending packet (%s)", log_strdup(UDP_MESSAGE));
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
    LOG_DBG("Connected, sending message");
    err = send(sock, UDP_MESSAGE, strlen(UDP_MESSAGE), 0);
    if (err < strlen(UDP_MESSAGE))
    {
        LOG_ERR("Error sending: %d", err);
        close(sock);
        return;
    }

    LOG_DBG("Message sent, waiting for downstream message");

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
        if (err < 0)
        {
            LOG_ERR("Error receiving: %d", err);
            close(sock);
            return;
        }
        if (err > 0)
        {
            buffer[err] = 0;
            LOG_INF("Received %d bytes (%s)", err, log_strdup(buffer));
        }
    }
    close(sock);
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
    LOG_INF("Returned from fota_init()");
    // Loop forever
    while (true)
    {
        k_sleep(1000);
    }
}

#define COAP_HOST "172.16.15.14"
#define COAP_PORT 5683
#define COAP_PAYLOAD "Hello there, I'm sent via CoAP"
#define COAP_PATH "coap/zephyr"

char payload[100];

void coapTest()
{
#define CBUF_LEN 100
    char cbuffer[CBUF_LEN];
    int err;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
        LOG_ERR("Error opening socket: %d", sock);
        return;
    }

    LOG_INF("Sending CoAP packet");
    static struct sockaddr_in remote_addr = {
        sin_family : AF_INET,
        sin_port : htons(COAP_PORT),
    };
    net_addr_pton(AF_INET, COAP_HOST, &remote_addr.sin_addr);
    err = connect(sock, (struct sockaddr *)&remote_addr, sizeof(remote_addr));
    if (err < 0)
    {
        LOG_ERR("Unable to connect to backend: %d", err);
        close(sock);
        return;
    }

    struct coap_packet p;
    coap_packet_init(&p, payload, sizeof(payload), 1, COAP_TYPE_CON, 8, coap_next_token(), COAP_METHOD_POST, coap_next_id());
    coap_packet_append_option(&p, COAP_OPTION_URI_PATH, COAP_PATH, strlen(COAP_PATH));
    coap_packet_append_payload_marker(&p);
    coap_packet_append_payload(&p, (u8_t *)COAP_PAYLOAD, strlen(COAP_PAYLOAD));

    err = send(sock, p.data, p.offset, 0);
    if (err < 0)
    {
        LOG_ERR("Error sending CoAP packet to backend: %d", err);
        close(sock);
        return;
    }
    LOG_INF("CoAP packet sent (%d bytes). Waiting for reply", p.offset);
    // Wait for ack from backend
    err = recv(sock, cbuffer, CBUF_LEN, 0);
    if (err < 0)
    {
        LOG_ERR("Error calling recv(): %d", err);
        close(sock);
        return;
    }

    err = coap_packet_parse(&p, cbuffer, err, NULL, 0);
    if (err < 0)
    {
        LOG_ERR("Error parsing CoAP response: %d", err);
        close(sock);
        return;
    }
    LOG_INF("Response: %02x", coap_header_get_code(&p));
    close(sock);
}

void main(void)
{
    udpTest();

    LOG_DBG("Halting firmware");
}