#include <stdio.h>

#include "comms.h"

// response from
// - AT+NRB
// - AT+CGPADDR
// - AT+CGPADDR
// - AT+NSOCR / +UFOTAS URC
// - AT+NSOST
// - AT+NSOST / +NSOMNI URC
// - AT+NSORF
// - AT+NSOCL
static char *data = "REBOOTING\r\nrandom junk goes here with more than one junk like exceeding buffer\r\nOK\r\n+CGPADDR: 0\r\nOK\r\n+CGPADDR: 0,\"1.2.3.4\"\r\nOK\r\n4\r\n+UFOTAS:0\r\nOK\r\n0,32\r\nOK\r\n+NSONMI:0,14\r\n0,31\r\nOK\r\n1,\"192.158.5.1\",1024,10,\"AABBCCDDEEAABBCCDDEE\",0\r\nOK\r\n";
static int index = 0;

int modem_read(uint8_t *b, int32_t timeout)
{
    if (data[index] == 0)
    {
        return -2;
    }
    *b = data[index++];
    return 0;
}
