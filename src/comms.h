#ifndef APP_COMMS_H
#define APP_COMMS_H

#define UART_COMMS 1
//#define I2C_COMMS 1

// Length of output buffer in result commands. 128 might be on the short side
#define RESULT_BUFFER_SIZE 128

#define MODEM_ERROR 0
#define MODEM_TIMEOUT -1
#define MODEM_OK (1)

struct modem_result
{
    void *fifo_reserved; // This is reserved for the FIFO
    char buffer[RESULT_BUFFER_SIZE];
};

/**
 * @brief Wait for result from modem command
 * @retval Status code: MODEM_ERROR, MODEM_OK or MODEM_TIMEOUT
 */
int modem_get_result(s32_t timeout);

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
bool modem_read(struct modem_result *result);

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

// TODO: implement a modem_begin() modem_end() to signal start and stop writes.
// The URC for incoming data requires access to the modem exclusively to avoid
// stepping on each others toes. There's added complexity wrt the receive queue
// so we should make sure that the queue is empty before doing the requisite
// NSORF command after the NSOMNI URC. Unfortunately the returned data is not
// in URC format so we can't filter it out in the background.
//
//int modem_get_urc_data(int fd, void *buf, size_t *len, char *remote_address, int remote_port);
#endif