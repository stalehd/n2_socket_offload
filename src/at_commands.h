#pragma once

#include <zephyr.h>
#include <net/socket.h>

/**
 * @brief Callback for receive notifications.
 */
typedef void (*at_callback_t)(int fd, size_t bytes);

/**
 * @brief Set callback function for new data notifications. This function is
 *        called whenever a +NSOMNI message is received from the modem.
 * @note  Only a single callback can be registered.
 */
void receive_callback(at_callback_t receive_cb);

/**
 * @brief  Decode AT+NSORF response. The buffer is read in multiple chunks from
 *         the modem.
 * @return -1 for ERROR response, -2 for timeout, number of bytes decoded otherwise
 * @note   Will swallow URCs and call the appropriate callbacks
 */
int atnsorf_decode(u8_t *buffer, size_t len, struct sockaddr *from, socklen_t *fromlen);

/**
 * @brief  Decode AT+NSOCR response. Reads until OK or ERROR is received.
 * @return socket file descriptor for modem, -1 for ERROR response, -2 for timeout
 * @note   Will swallow URCs and call the appropriate callbacks
 */
int atnsocr_decode(int *sockfd);

/**
 * @brief  Decode AT+NSOCL response. Reads until OK or ERROR is received.
 * @return 0 for OK, -1 for ERROR
 * @note   Will swallow URCs and call the appropriate callbacks
 */
int atnsocl_decode();

/**
 * @brief  Decode AT+NSOST response. Reads until OK or ERROR is received.
 * @return 0 for OK, -1 for ERROR, -2 for timeout
 * @note   Will swallow URCs and call the appropriate callbacks
 */
int atnsost_decode(int *sockfd, size_t *len);

/**
 * @brief Reads response from AT+NRB command. Reads until OK or ERROR is received.
 * @note  Will swallow URCs and call the appropriate callbacks
 */
int atnrb_decode();
