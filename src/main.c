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

#include <zephyr.h>
#include <drivers/gpio.h>
#include <net/socket.h>
#include "fota.h"
#include "test_udp.h"
#include "test_coap.h"
#include "test_modem.h"
#include "panic.h"

void testFOTA()
{
    // Initialize the application and run any self-tests before calling fota_init.
    // Otherwise, if initialization or self-tests fail after an update, reboot the system and the previous firmware image will be used.
    k_sleep(1000);
    int ret = fota_init();
    if (ret)
    {
        printf("fota_init: %d\n", ret);
        return;
    }
    printf("Returned from fota_init()\n");
    // Loop forever
    #if 1
     while (true)
    {
        k_sleep(5000);
    }
    #else

    char buf[12];
    int counter = 0;

    int err = 0;
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
    if (err < 0) {
        printf("Error connecting: %d", err);
        close(sock);
        return;
    }
    while (true)
    {
        sprintf(buf, "Keepalive%d", counter);
        err = send(sock, buf, strlen(buf), 0);
        if (err < strlen(buf))
        {
            printf("Error sending: %d\n", err);
            close(sock);
            return;
        }
        printf("%d:%s\n", sock, buf);
        // Send keepalive messages every 5 seconds
        k_sleep(5000);
        counter++;
    }
    close(sock);
    #endif
}

void main(void)
{

    //init_panic();

    printf("Start\n");

    testFOTA();

    printf("Halting firmware\n");
}
