#include "config.h"
#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(comms);

#include <zephyr.h>
#include <device.h>
#include <uart.h>
#include <kernel.h>
#include <sys/ring_buffer.h>
#include <errno.h>
#include <stdlib.h>
#include "comms.h"
#include "at_commands.h"

// Ring buffer size
#define RB_SIZE 256

// Underlying ring buffer
static u8_t buffer[RB_SIZE];

// Ring buffer for received data
static struct ring_buf rx_rb;

// Semaphore for incoming data
static struct k_sem rx_sem;

static struct device *uart_dev;

/*
 * Modem comms. It's quite a mechanism - the UART is read from an ISR, then
 * sent to a processing thread via a ring buffer. The processing thread
 * parses the incoming data stream and when OK or ERROR is received the
 * data is forwarded to the consuming library via modem_read_line.
 */

/**
 * @brief The ISR for UART rx
 */
static void uart_isr(void *user_data)
{
    struct device *dev = (struct device *)user_data;
    uint8_t data;
    int rx, rb;
    while (uart_irq_update(dev) &&
           uart_irq_rx_ready(dev))
    {
        rx = uart_fifo_read(dev, &data, 1);
        if (rx == 0)
        {
            return;
        }
        //printf("%c", data);
        rb = ring_buf_put(&rx_rb, &data, 1);
        if (rb != rx)
        {
            LOG_ERR("RX buffer is full. Bytes pending: %d, written: %d", rx, rb);
            //k_sem_give(&rx_sem);
            return;
        }
        k_sem_give(&rx_sem);
    }
}

void modem_write(const char *cmd)
{
    u8_t *buf = (u8_t *)cmd;
    size_t data_size = strlen(cmd);
    do
    {
        uart_poll_out(uart_dev, *buf++);
    } while (--data_size);
}

bool modem_read(uint8_t *b, int32_t timeout)
{
    switch (k_sem_take(&rx_sem, timeout))
    {
    case 0:
        if (ring_buf_get(&rx_rb, b, 1) == 1)
        {
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

void modem_init(void)
{
    k_sem_init(&rx_sem, 0, RB_SIZE);
    ring_buf_init(&rx_rb, RB_SIZE, buffer);

    uart_dev = device_get_binding("UART_0");
    if (!uart_dev)
    {
        LOG_ERR("Unable to load UART device. GPS Thread cannot continue.");
        return;
    }
    uart_irq_callback_user_data_set(uart_dev, uart_isr, uart_dev);
    uart_irq_rx_enable(uart_dev);
    LOG_DBG("UART device loaded.");
}

bool modem_is_ready()
{
    modem_write("AT+CGPADDR\r\n");
    char ip[16];
    size_t len = 0;
    if (atcgpaddr_decode((char *)&ip, &len) == AT_OK)
    {
        if (len > 1)
        {
            return true;
        }
    }
    return false;
}

void modem_restart()
{
    modem_write("AT+NRB\r\n");
    // just wait for OK or ERROR - don't care about the result (yet)
    atnrb_decode();
}