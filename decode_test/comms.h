#pragma once
#include <inttypes.h>
/**
 * @brief Read a single character from the modem.
 */
int modem_read(uint8_t *b, int32_t timeout);
