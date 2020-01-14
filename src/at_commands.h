#ifndef AT_COMMANDS_H
#define AT_COMMANDS_H

#include <zephyr.h>
#include <net/socket.h>

/**
 * @brief Decode AT+NSORF response. The buffer is read in multiple chunks from
 *        the modem.
 */
int atnsorf_decode(u8_t *buffer, size_t len, struct sockaddr *from, socklen_t *fromlen);

#endif
