#include "config.h"
#include <logging/log.h>
#define LOG_LEVEL APP_LOG_LEVEL
LOG_MODULE_REGISTER(comms);

#include <zephyr.h>
#include <device.h>
#include <uart.h>
#include <kernel.h>
#include <sys/ring_buffer.h>
#include <errno.h>

#define RB_SIZE 512
static u8_t buffer[RB_SIZE];
static struct ring_buf rx_rb;
static struct k_sem rx_sem;

int modem_read(char *buf, int max_len, s32_t timeout)
{
    return 0;
}

/**
 * @brief The ISR for UART rx
 */
static void uart_isr(void *user_data)
{
    struct device *dev = (struct device *)user_data;
    u8_t data;

    int rx, rb;
    while (uart_irq_update(dev) &&
           uart_irq_rx_ready(dev))
    {
        rx = uart_fifo_read(dev, &data, 1);
        if (rx == 0)
        {
            return;
        }
        rb = ring_buf_put(&rx_rb, &data, 1);
        if (rb != rx)
        {
            LOG_ERR("RX buffer is full. Bytes pending: %d, written: %d", rx, rb);
            k_sem_give(&rx_sem);
            return;
        }
        k_sem_give(&rx_sem);
    }
}

static struct device *uart_dev;

void modem_write(const char *cmd)
{
    u8_t *buf = (u8_t *)cmd;
    size_t data_size = strlen(cmd);
    do
    {
        uart_poll_out(uart_dev, *buf++);
    } while (--data_size);
}

/**
 * @brief Process a single line received from the modem.
 * @note The following URCs are handled and filtered by this function:
 *          - CEREG (1 = connected, 2 = connecting)
 *          - NSONMI (incoming UDP)
 *          - NPSMR (power save mode 0 normal, 1 power save)
 *          - CSCON (connection status, will probably be discarded)
 */
static void process_line(const char *buffer, const size_t length)
{
    if (length == 0)
    {
        LOG_ERR("Length = 0 for data");
        return;
    }

    if (strncmp(buffer, "OK", 2) == 0)
    {
        // Completed response successfully - make a new response buffer
        LOG_INF("Got sucessful response");
        return;
    }
    if (strncmp(buffer, "ERROR", 5) == 0)
    {
        // Completed response with failure - make a new response buffer
        LOG_ERR("Got error response");
        return;
    }
    LOG_INF("Received: %s", log_strdup(buffer));
}

/**
 * @brief RX thread. This grabs the data from the ring buffer, splits into
 *        lines and processes the output. URCs are handled automatically while
 *        responses are aggregated and returned in a queue. A new entry in the
 *        queue is added whenever OK or ERROR is read.
 */
void modem_rx_thread(void)
{
    char prev = ' ', cur = ' ';
    char current_line[256];
    size_t current_index = 0;
    while (true)
    {
        k_sem_take(&rx_sem, K_FOREVER);
        // read single chars into ring buffer and split on CR LF
        if (ring_buf_get(&rx_rb, &cur, 1))
        {
            if (cur == '\n' && prev == '\r')
            {
                // this is a new line, process the current line and clear it
                if (current_index != 0)
                {
                    current_line[current_index] = 0;
                    process_line(current_line, current_index);
                    current_index = 0;
                }
                continue;
            }
            // Skip CR chars
            if (cur == '\r')
            {
                prev = cur;
                continue;
            }
            // append to line
            current_line[current_index++] = cur;
            prev = cur;
        }
    }
}
#define RX_THREAD_STACK 500
#define RX_THREAD_PRIORITY 5

/* RX thread structures */
K_THREAD_STACK_DEFINE(rx_thread_stack,
                      RX_THREAD_STACK);
struct k_thread rx_thread;

void init_comms(void)
{
    k_sem_init(&rx_sem, 0, RB_SIZE);
    ring_buf_init(&rx_rb, RB_SIZE, buffer);

    k_thread_create(&rx_thread, rx_thread_stack,
                    K_THREAD_STACK_SIZEOF(rx_thread_stack),
                    (k_thread_entry_t)modem_rx_thread,
                    NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);

    uart_dev = device_get_binding("UART_0");
    if (!uart_dev)
    {
        LOG_ERR("Unable to load UART device. GPS Thread cannot continue.");
        return;
    }
    uart_irq_callback_user_data_set(uart_dev, uart_isr, uart_dev);
    uart_irq_rx_enable(uart_dev);
    LOG_INF("UART device loaded.");
}
