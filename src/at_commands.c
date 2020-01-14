#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_N2
LOG_MODULE_REGISTER(n2_decoder);

#include "at_commands.h"
#include "comms.h"
#include <zephyr.h>
#include <net/socket.h>
/**
 * Decode AT commands from the N2 modems. Rather than implement a full AT
 * library it's easier just to process the strings as is.
 */

#define FROM_HEX(x) (x - '0' > 9 ? x - 'A' + 10 : x - '0')

/**
 * @brief read response NSORF from the modem. This is done (almost) byte by byte
 *        to save memory. The response might be split into several lines if the
 *        buffer is > 64 bytes.
 */
int atnsorf_decode(u8_t *buffer, size_t len, struct sockaddr *from, socklen_t *fromlen)
{
    struct modem_result result;
    u16_t received = 0;
    u8_t field_no = 0;
    int strpos = 0;
    char tmpstr[32];
    int bufferpos = 0;
    memset(buffer, 0, len);
    if (fromlen)
    {
        *fromlen = sizeof(struct sockaddr_in);
    }
    while (modem_read(&result) && received <= len)
    {
        for (int ch = 0; ch < strlen(result.buffer); ch++)
        {
            if (result.buffer[ch] == 0)
            {
                // end of line
                break;
            }
            if (result.buffer[ch] == ',')
            {
                switch (field_no)
                {
                case 1:
                    tmpstr[strpos] = 0;
                    if (from)
                    {
                        if (!inet_pton(AF_INET, tmpstr, &((struct sockaddr_in *)from)->sin_addr))
                        {
                            LOG_ERR("Could not convert IP address from string (inet_pton). Address = %s", log_strdup(tmpstr));
                        }
                    }
                    break;
                case 2:
                    tmpstr[strpos] = 0;
                    if (from)
                    {
                        ((struct sockaddr_in *)from)->sin_family = AF_INET;
                        ((struct sockaddr_in *)from)->sin_port = htons(atoi(tmpstr));
                    }
                    break;

                case 3:
                    tmpstr[strpos] = 0;
                default:
                    break;
                }
                strpos = 0;
                field_no++;
                continue;
            }
            switch (field_no)
            {
            case 0:
                // this is the file descriptor. Ignore
                break;
            case 1:
                // IP address - skip quotes
                if (result.buffer[ch] != '"')
                {
                    tmpstr[strpos++] = result.buffer[ch];
                }
                break;
            case 2:
                tmpstr[strpos++] = result.buffer[ch];
                // port
                break;
            case 3:
                // length of data
                tmpstr[strpos++] = result.buffer[ch];
                break;
            case 4:
                // the data itself
                if (result.buffer[ch] == '"')
                {
                    continue;
                }
                tmpstr[strpos++] = result.buffer[ch];
                if (strpos == 2)
                {
                    // we've got a byte
                    buffer[bufferpos++] = (FROM_HEX(tmpstr[0]) << 4) | FROM_HEX(tmpstr[1]);
                    strpos = 0;
                }
                break;
            case 5:
                // remaining bytes
                break;
            default:
                return bufferpos;
            }
        }
        // first element is
    }
    return bufferpos;
}