#ifndef _APP_CONFIG_H
#define _APP_CONFIG_H

#define APP_LOG_LEVEL LOG_LEVEL_DBG

#define UART_COMMS 1
//#define I2C_COMMS 1

// 2 second timeout for commands. This is ample time<
#define MDM_CMD_TIMEOUT 2

#define CONFIG_N2_NAME "SARA_N2"
#define CONFIG_N2_INIT_PRIORITY 80
#define CONFIG_N2_MAX_PACKET_SIZE 512

#endif