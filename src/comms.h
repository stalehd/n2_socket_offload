#pragma once

#define UART_COMMS 1
//#define I2C_COMMS 1

// Length of output buffer in result commands. 128 might be on the short side
#define RESULT_BUFFER_SIZE 128

struct modem_result
{
    void *fifo_reserved; // This is reserved for the FIFO
    char buffer[RESULT_BUFFER_SIZE];
};

/**
 * @brief Writes a string to the modem.
 * @param *cmd: The string to send
 */
void modem_write(const char *cmd);

/**
 * @brief Read a single line from the modem. The line does not include newline
 *        or carriage returns.
 * @param *buf: The buffer to read into
 *        *max_len: Length of buffer
 *        timeout: max time to wait for response
 * @retval True if a line is read
 */
bool modem_read_and_no_error(struct modem_result *result, s32_t timeout);


/**
 * @brief Initialize communications
 */
void modem_init(void);

/**
 * @brief Reboot modem
 */
void modem_restart(void);

/**
 * @brief Check if modem is online and ready to use
 * @retval True when an IP address is assigned.
 */
bool modem_is_ready(void);

/**
 * @brief Callback for receive notifications.
 */
typedef void (*receive_callback_t)(int fd, size_t bytes);

/**
 * @brief Set callback function for new data notifications. This function is
 *        called whenever a +NSOMNI message is received from the modem.
 * @note  Only a single callback can be registered.
 */
void modem_receive_callback(receive_callback_t receive_cb);

/**
 * @brief Read a single character from the modem.
 */
int modem_read(u8_t *b, s32_t timeout);
