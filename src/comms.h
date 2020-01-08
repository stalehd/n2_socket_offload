#ifndef APP_COMMS_H
#define APP_COMMS_H

#define UART_COMMS 1
//#define I2C_COMMS 1

int modem_write(const char *cmd);
int modem_read(const char *buf, int max_len);
void init_comms();

#endif