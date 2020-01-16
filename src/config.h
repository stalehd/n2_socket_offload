#pragma once

#define APP_LOG_LEVEL LOG_LEVEL_DBG
#define LOG_LEVEL_N2 LOG_LEVEL_INF
#define UART_COMMS 1
//#define I2C_COMMS 1

// 2 second timeout for commands. This is ample time<
#define MDM_CMD_TIMEOUT 2

#define CONFIG_N2_NAME "SARA_N2"
// Priority should be higher than the lwm2m service
#define CONFIG_N2_INIT_PRIORITY 35
#define CONFIG_N2_MAX_PACKET_SIZE 512

