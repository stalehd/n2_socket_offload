#pragma once

#define UART_COMMS 1
//#define I2C_COMMS 1


/**
 * @brief Initialize communications
 */
void modem_init(void);

/**
 * @brief Writes a string to the modem.
 * @param *cmd: The string to send
 */
void modem_write(const char *cmd);

/**
 * @brief Read a single character from the modem.
 */
bool modem_read(uint8_t *b, int32_t timeout);


bool modem_is_ready();

void modem_restart();