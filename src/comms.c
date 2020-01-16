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

// Ring buffer size
#define RB_SIZE 128

// Underlying ring buffer
static u8_t buffer[RB_SIZE];

// Ring buffer for received data
static struct ring_buf rx_rb;

// Semaphore for incoming data
static struct k_sem rx_sem;

// Results queue. Each line is an element. The OK and ERROR
// lines are not included.
struct k_fifo results;

static struct device *uart_dev;

// Callback for receive notifications
static receive_callback_t receive_callback = NULL;

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

static void flush_stale_results()
{
    // Flush the FIFO for any old commands
    struct modem_result *stale_result;
    do
    {
        stale_result = k_fifo_get(&results, K_NO_WAIT);
        if (stale_result)
        {
            LOG_DBG("Flushing stale results from old command");
            k_free(stale_result);
        }
    } while (stale_result);
}

void modem_write(const char *cmd)
{
    flush_stale_results();
    u8_t *buf = (u8_t *)cmd;
    size_t data_size = strlen(cmd);
//    LOG_DBG("Write %s", log_strdup(cmd));
    do
    {
        uart_poll_out(uart_dev, *buf++);
    } while (--data_size);
}

bool modem_read_and_no_error(struct modem_result *result, s32_t timeout)
{
    struct modem_result *rx;

    rx = k_fifo_get(&results, timeout);
    if (!rx)
    {
        return false;
    }
    // ERROR response is an error. There are no commands that return a result *and* ERROR
    // so the ERROR response will be the first response.
    if (strncmp(rx->buffer, "ERROR", 5) == 0) {
        return false;
    }
    strcpy(result->buffer, rx->buffer);
    k_free(rx);
    return true;
}

/**
 * @brief process URCs
 * @note  The following URCs are handled and filtered by this function:
 *          - CEREG (1 = connected, 2 = connecting)
 *          - NSONMI (incoming UDP)
 *          - NPSMR (power save mode 0 normal, 1 power save)
 *          - CSCON (connection status, will probably be discarded)
 *          - UFOTAS (FOTA status, reported on reboot)
 */
static bool process_urc(const char *buffer)
{
    if (strncmp("+CEREG", buffer, 6) == 0)
    {
        LOG_DBG("Network registration URC: %s", log_strdup(buffer));
        return true;
    }
    if (strncmp("+NSOMNI", buffer, 7) == 0)
    {
        LOG_INF("Input data URC: %s", log_strdup(buffer));
        // TODO: Extract socket descriptor and number of bytes, signal to client
        int fd = 1;
        size_t bytes = 100;
        receive_callback(fd, bytes);
        return true;
    }
    if (strncmp("+NPSMR", buffer, 6) == 0)
    {
        LOG_DBG("Power save URC: %s", log_strdup(buffer));
        return true;
    }
    if (strncmp("+CSCON", buffer, 6) == 0)
    {
        LOG_DBG("Connection status URC: %s", log_strdup(buffer));
        return true;
    }
    if (strncmp("+UFOTAS", buffer, 7) == 0)
    {
        LOG_DBG("FOTA status URC: %s", log_strdup(buffer));
        return true;
    }
    // Won't handle this
    return false;
}

/**
 * @brief Process a single line received from the modem.
 * @note URCs are skipped from the input
 */
static void process_line(const char *buffer, const size_t length)
{
    //LOG_DBG("Process: %s", log_strdup(buffer));
    if (length == 0)
    {
        LOG_ERR("Length = 0 for data");
        return;
    }

    if (buffer[0] == '+' && process_urc(buffer))
    {
        return;
    }

    LOG_DBG("Received: %s", log_strdup(buffer));
    // I'm not checking the return from k_malloc since this will just panic
    // which is more or less what we want if we run out of heap.
    struct modem_result *new_item = k_malloc(sizeof(struct modem_result));
    strncpy(new_item->buffer, buffer, RESULT_BUFFER_SIZE);
    k_fifo_put(&results, new_item);
}

/**
 * @brief RX thread. This grabs the data from the ring buffer, splits into
 *        lines and processes the output. URCs are handled automatically while
 *        responses are aggregated and returned in a queue. A new entry in the
 *        queue is added whenever OK or ERROR is read.
 */
void modem_rx_thread(void)
{
#define MAX_LINE_LENGTH 256
    char prev = ' ', cur = ' ';
    char current_line[MAX_LINE_LENGTH];
    size_t current_index = 0;
    while (true)
    {
        k_sem_take(&rx_sem, K_FOREVER);
        // read single chars into ring buffer and split on CR LF
        if (ring_buf_get(&rx_rb, &cur, 1))
        {
            if ((cur == '\n' && prev == '\r') || current_index == MAX_LINE_LENGTH)
            {
                // this is a new line, process the current line and clear it
                if (current_index != 0)
                {
                    current_line[current_index] = 0;
                    LOG_INF("Got %d bytes from modem", current_index);
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
#define RX_THREAD_STACK 2048
#define RX_THREAD_PRIORITY (CONFIG_NUM_COOP_PRIORITIES)

struct k_thread rx_thread;

/* RX thread structures */
K_THREAD_STACK_DEFINE(rx_thread_stack,
                      RX_THREAD_STACK);

void modem_init(void)
{
    k_sem_init(&rx_sem, 0, RB_SIZE);
    k_fifo_init(&results);
    ring_buf_init(&rx_rb, RB_SIZE, buffer);

    k_thread_create(&rx_thread, rx_thread_stack,
                    K_THREAD_STACK_SIZEOF(rx_thread_stack),
                    (k_thread_entry_t)modem_rx_thread,
                    NULL, NULL, NULL, K_PRIO_COOP(RX_THREAD_PRIORITY), 0, K_NO_WAIT);

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

#define REBOOT_TIMEOUT 5000

void modem_restart(void)
{
    flush_stale_results();
    modem_write("AT+NRB\r\n");

    // Will just read and discard the results until OK or ERROR is received
    struct modem_result *rx;


    do {
        rx = k_fifo_get(&results, REBOOT_TIMEOUT);
        if (!rx)
        {
            return;
        }

        if (strncmp(rx->buffer, "ERROR", 5) == 0) {
            k_free(rx);
            LOG_ERR("AT+NRB returned error");
            return;
        }
        if (rx->buffer[0] == 'O' && rx->buffer[1] == 'K') {
            k_free(rx);
            return;
        }
        k_free(rx);
    } while (rx);
}

bool modem_is_ready(void)
{
    flush_stale_results();
    modem_write("AT+CGPADDR\r\n");

    struct modem_result result;
    if (!modem_read_and_no_error(&result, 2500))
    {
        LOG_ERR("Modem did not respond on AT+CGPADDR command");
        return false;
    }
    char *endstr = NULL;
    char *ptr = result.buffer;
    // String should contain +CGPADDR: 0,"<ip>"
    while (*ptr)
    {
        if (*ptr == ',')
        {
            endstr = ptr + 1;
            break;
        }
        ptr++;
    }

    if (!endstr || !*ptr)
    {
        // No address here
        return false;
    }

    // This bit might be omitted if the (local) address isn't used.

    // Remove the quotation marks
    endstr++;
    ptr = endstr;
    while (*ptr)
    {
        if (*ptr == '"')
        {
            *ptr = 0;
            break;
        }
        ptr++;
    }

    // Won't do anything with the address here. Just print it. Might be nice to
    // store it somewhere but there's really no use for it in the firmware. Yet.
    LOG_DBG("Address: %s", log_strdup(endstr));

    return true;
}

void modem_receive_callback(receive_callback_t recv_cb)
{
    receive_callback = recv_cb;
}
