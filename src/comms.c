#include "config.h"
#include <logging/log.h>
#define LOG_LEVEL APP_LOG_LEVEL
LOG_MODULE_REGISTER(comms);

#include <zephyr.h>
#include <device.h>
#include <uart.h>

int modem_read(const char *buf, int max_len)
{
    return 0;
}

static void uart_fifo_callback(void *user_data)
{
    struct device *dev = (struct device *)user_data;
    u8_t data;
    int err = uart_irq_update(dev);
    if (err != 1)
    {
        LOG_ERR("uart_fifo_callback. uart_irq_update failed with error: %d (expected 1)", err);
        return;
    }
    if (uart_irq_rx_ready(dev))
    {
        uart_fifo_read(dev, &data, 1);
        printk("%c", data);
    }
}

static struct device *uart_dev;

int modem_write(const char *cmd)
{
    LOG_DBG("Start write (%d bytes)", strlen(cmd));
    u8_t *buf = (u8_t *)cmd;
    size_t data_size = strlen(cmd);
    do
    {
        uart_poll_out(uart_dev, *buf++);
    } while (--data_size);

    return 0;
}

void init_comms()
{
    uart_dev = device_get_binding("UART_0");
    if (!uart_dev)
    {
        LOG_ERR("Unable to load UART device. GPS Thread cannot continue.");
        return;
    }
    uart_irq_callback_user_data_set(uart_dev, uart_fifo_callback, uart_dev);
    uart_irq_rx_enable(uart_dev);
    LOG_INF("UART device loaded.");
}
