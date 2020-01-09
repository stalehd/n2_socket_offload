#ifndef APP_COMMS_H
#define APP_COMMS_H

#define UART_COMMS 1
//#define I2C_COMMS 1

/**
 * @brief Writes a string to the modem.
 * @param *cmd: The string to send
 */
void modem_write(const char *cmd);

/**
 * @brief Read response from modem.
 * @param *buf: The buffer to read into
 *        *max_len: Length of buffer
 *        timeout: max time to wait for response
 * @retval Number of bytes read.
 */
int modem_read(const char *buf, int max_len, s32_t timeout);

/**
 * @brief Initialize communications
 */
void init_comms(void);

#endif