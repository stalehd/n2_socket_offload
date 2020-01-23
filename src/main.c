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

#include "fota.h"
#include "test_udp.h"
#include "test_coap.h"
#include "test_modem.h"

void testNoGodNoPleaseNoNoooooooooFOTA()
{
    // Initialize the application and run any self-tests before calling fota_init.
    // Otherwise, if initialization or self-tests fail after an update, reboot the system and the previous firmware image will be used.
    k_sleep(1000);
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

void main(void)
{
    LOG_DBG("Start");

    testUDP();

    LOG_DBG("Halting firmware");
}
