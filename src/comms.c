#include "config.h"

int modem_write(const char *cmd)
{
#ifdef UART_COMMS
    return 0;
#endif
#ifdef I2C_COMMS
    return 0;
#endif
}
int modem_read(const char *buf, int max_len)
{
#ifdef UART_COMMS
    return 0;
#endif
#ifdef I2C_COMMS
    return 0;
#endif
}
