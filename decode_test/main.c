#include <stdio.h>
#include <string.h>
#include "at_commands.h"

void at_callback(int fd, size_t bytes) {
    printf("Got receive callback. fd=%d bytes=%lu\n", fd, bytes);
}

int main(void)
{
    receive_callback(at_callback);

    switch (atnrb_decode()) {
        case 0:
            printf("Successful reboot\n");
            break;
        case -1:
            printf("ERROR response\n");
            return 1;
        default:
            printf("Timeout reading\n");
            return 1;
    }

    char addr[16];
    size_t len = 0;
    switch (atcgpaddr_decode(addr, &len)) {
        case 0:
            if (len != 1 || strcmp(addr, "0") < 0) {
                printf("Unexpected address #1: \"%s\" (len = %lu)\n", addr, len);
                return 1;
            }
            printf("Found address \"%s\" (len=%lu)\n", addr, len);
            break;
        default:
            printf("Timeout/error reading CGPADDR #1\n");
            return 1;
    }
    len = 0;
    switch (atcgpaddr_decode(addr, &len)) {
        case 0:
            if (len != 7 || strcmp(addr, "1.2.3.4") < 0) {
                printf("Unexpected address #2: \"%s\" (len = %lu)\n", addr, len);
                return 1;
            }
            printf("Found address \"%s\" (len=%lu)\n", addr, len);
            break;
        default:
            printf("Timeout/error reading CGPADDR #2\n");
            return 1;
    }
    int fd = 1;
    switch (atnsocr_decode(&fd)) {
        case 0:
            if (fd != 4) {
                printf("Invalid fd (%d)\n", fd);
                return 1;
            }
            printf("NSOCR OK (fd=%d)\n", fd);
            break;
        default:
            printf("Timeout/error reading NSOCR result\n");
            return 1;
    }
    fd = 9;
    len = 0;
    switch (atnsost_decode(&fd,&len)) {
        case 0:
            if (fd != 0 && len != 32) {
                printf("Invalid NSOST #1 return: fd=%d len=%lu\n", fd, len);
                return 1;
            }
            printf("NSOST #1 OK (fd=%d len=%lu)\n", fd, len);
            break;
        default:
            printf("Timeout/error reading NSOST #1 result\n");
            return 1;
    }
    fd = 90;
    len = 90;
    switch (atnsost_decode(&fd, &len)) {
        case 0:
            if (fd != 0 && len != 31) {
                printf("Invalid NSOST #2 return: fd=%d len=%lu\n", fd, len);
                return 1;
            }
            printf("NSOST #2 OK (fd=%d len=%lu)\n", fd, len);
            break;
        default:
            printf("Timeout/error reading NSOST #2 result\n");
            return 1;
    }
    fd = 90;
    char ip[16];
    int port = 999;
    len = 999;
    uint8_t data[128];
    size_t remaining = 999;
    const uint8_t expected_data[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE};
    switch (atnsorf_decode(&fd, (char*)&ip, &port, (uint8_t *)&data, &remaining)) {
        case 0:
            if (fd != 1 || strcmp(ip, "192.158.5.1") != 0 || port != 1024 || remaining != 0) {
                printf("Invalid NSORF return: fd=%d, ip=%s port=%d remaining=%lu\n", fd, ip, port, remaining);
                return 1;
            }
                for (int i = 0; i < sizeof(expected_data); i++) {
                    if (expected_data[i] != data[i]) {
                        printf("Data error at index %d\n", i);
                        return 1;
                    }
                }
            printf("NSORF OK (fd=%d)\n", fd);
            break;
        default:
            printf("Timeout/error reading NSORF result\n");
            return 1;
    }
    return 0;
}