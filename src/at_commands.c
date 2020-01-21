#include <zephyr.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include "at_commands.h"
#include "comms.h"

#include <logging/log.h>
#define LOG_LEVEL LOG_LEVEL_DBG
LOG_MODULE_REGISTER(at_commands);

#define CMD_TIMEOUT K_MSEC(10000)

// Ring buffer size. The ring buffer holds just enough bytes to detect the
// different responses/fields.
#define B_SIZE 10

struct buf
{
    uint8_t data[B_SIZE];
    uint8_t index;
    uint16_t size;
};

void b_reset(struct buf *rb)
{
    rb->index = 0;
    rb->size = 0;
    // this can be skipped
    memset(rb->data, 0, B_SIZE);
}

void b_init(struct buf *rb)
{
    b_reset(rb);
}
void b_add(struct buf *rb, uint8_t b)
{
    if (b == '\r' || b == '\n')
    {
        return;
    }
    if (rb->index < B_SIZE)
    {
        rb->data[rb->index++] = b;
    }
    rb->size++;
}

bool b_is(struct buf *rb, const char *str, const size_t len)
{
    if (strncmp((char *)rb->data, str, len) == 0)
    {
        return true;
    }
    return false;
}

bool b_is_urc(struct buf *rb)
{
    return (rb->size > 0 && rb->data[0] == '+');
}

// Callbacks for the input processing. The context is used to maintain variables
// between the invocations and are passed by the decode_input function below.
typedef void (*eol_callback_t)(void *ctx, struct buf *rb, bool is_urc);
typedef void (*char_callback_t)(void *ctx, struct buf *rb, char b, bool is_urc, bool is_space);

// Each decoder is largely the same so we use a strategy pattern for each. The char
// callback is called for each character input and the EOL callback is called when a
// new line is found. The buffer will contain the *first* 9 characters of the line
// so it might be truncated.
//
int decode_input(int32_t timeout, void *ctx, char_callback_t char_cb, eol_callback_t eol_cb)
{
    struct buf rb;
    b_init(&rb);
    uint8_t b, prev = ' ';
    bool is_urc = false;

    while (modem_read(&b, timeout))
    {
        if (b == '+' && rb.size == 0)
        {
            is_urc = true;
        }
        b_add(&rb, b);
        if (prev == '\r' && b == '\n')
        {
            if (eol_cb)
            {
                eol_cb(ctx, &rb, is_urc);
            }
            b_reset(&rb);
            is_urc = false;
        }
        if (char_cb)
        {
            char_cb(ctx, &rb, b, is_urc, isspace(b));
        }
        if (rb.size >= 2)
        {
            if (b_is(&rb, "OK", 2))
            {
                return AT_OK;
            }
            if (b_is(&rb, "ERROR", 5))
            {
                return AT_ERROR;
            }
        }
        // Additional URCs to support:
        //  - CEREG
        //  - NPSMR
        //  - CSCON
        //  - UFOTAS

        prev = b;
    }
    return AT_TIMEOUT;
}

int atnrb_decode()
{
    return decode_input(CMD_TIMEOUT, NULL, NULL, NULL);
}

// Just wait for OK or ERROR
int atnsocl_decode()
{
    return decode_input(CMD_TIMEOUT, NULL, NULL, NULL);
}

// Decode the CGPADDR response. The in_address flag says if we're in the address
// string (ie past the +CGPADDR: string) and copies the bytes into the address
// buffer. When the eol callback is set the address flag is reset

struct cgp_ctx
{
    bool in_address;
    uint8_t addrindex;
    char *address;
    char *buffer;
    size_t *len;
    uint8_t i;
};

void cgpaddr_eol(void *ctx, struct buf *rb, bool is_urc)
{
    struct cgp_ctx *c = (struct cgp_ctx *)ctx;
    if (c->in_address)
    {
        bool in_str = false;
        uint8_t n = 0;
        for (int i = 0; i < (c->i); i++)
        {
            if (in_str && c->buffer[i] == '\"')
            {
                in_str = false;
            }
            if (in_str)
            {
                c->address[n++] = c->buffer[i];
            }
            if (!in_str && (c->buffer[i] == '\"'))
            {
                in_str = true;
            }
        }
        c->address[n] = 0;
        *c->len = n;
    }
    c->in_address = false;
}

void cgpaddr_char(void *ctx, struct buf *rb, char b, bool is_urc, bool is_space)
{
    struct cgp_ctx *c = (struct cgp_ctx *)ctx;
    if (c->in_address && !is_space)
    {
        c->buffer[c->i++] = (char)b;
    }
    if (is_urc && rb->size == 9 && b_is(rb, "+CGPADDR:", 9))
    {
        c->in_address = true;
    }
}

int atcgpaddr_decode(char *address, size_t *len)
{
    char buffer[20];
    memset(buffer, 0, sizeof(buffer));
    struct cgp_ctx ctx = {
        .in_address = false,
        .address = address,
        .len = len,
        .buffer = buffer,
        .i = 0,
    };
    return decode_input(CMD_TIMEOUT, &ctx, cgpaddr_char, cgpaddr_eol);
}

// Decode NSCR responses. This is fairly straightforward since there's only
// a single digit that is returned. We'll pass the pointer to the return
// function as the context and assign it when the line ends.

void nsocr_eol(void *ctx, struct buf *rb, bool is_urc)
{
    if (!is_urc && rb->size > 0)
    {
        int *sockfd = (int *)ctx;
        *sockfd = atoi((const char *)rb->data);
    }
}

int atnsocr_decode(int *sockfd)
{
    *sockfd = -2;
    return decode_input(CMD_TIMEOUT, sockfd, NULL, nsocr_eol);
}

// Decode SOST responses. Also quite simple since everything fits into
// the entire 9-byte buffer so we just process the line at EOL.

struct nsost_ctx
{
    int *sockfd;
    size_t *len;
};

int atnsost_decode()
{
    return decode_input(CMD_TIMEOUT, NULL, NULL, NULL);
}

// Decode NSORF responses. Each field is decoded separately and stored off in
// a temporary buffer (except the data field which might be large).
#define FROM_HEX(x) (x - '0' > 9 ? x - 'A' + 10 : x - '0')

#define MAX_FIELD_SIZE 16

struct nsorf_ctx
{
    int *sockfd;
    char *ip;
    int *port;
    uint8_t *data;
    size_t *remaining;
    size_t *received;
    int fieldno;
    int fieldindex;
    char field[MAX_FIELD_SIZE];
    int dataidx;
};

void nsorf_eol(void *ctx, struct buf *rb, bool is_urc)
{
    struct nsorf_ctx *c = (struct nsorf_ctx *)ctx;
    if (!is_urc && c->fieldindex > 0)
    {
        *c->remaining = atoi(c->field);
    }
}

void nsorf_char(void *ctx, struct buf *rb, char b, bool is_urc, bool is_space)
{
    if (is_urc || is_space)
    {
        return;
    }
    struct nsorf_ctx *c = (struct nsorf_ctx *)ctx;

    switch (b)
    {
    case ',':
        c->fieldindex = 0;
        switch (c->fieldno)
        {
        case 0:
            *c->sockfd = atoi(c->field);
            break;
        case 1:
            strcpy(c->ip, c->field);
            break;
        case 2:
            *c->port = atoi(c->field);
            break;
        case 3:
            // ignore
            break;
        case 4:
            // ignore
            break;
        default:
            // Should not encounter field #5 here
            LOG_ERR("Too many fields (%d) in response\n", c->fieldno);
            return;
        }
        memset(c->field, 0, MAX_FIELD_SIZE);
        c->fieldno++;
        break;
    case '\"':
        break;
    default:
        c->field[c->fieldindex++] = b;
        if (c->fieldno == 4 && c->fieldindex == 2)
        {
            c->data[c->dataidx++] = (FROM_HEX(c->field[0]) << 4 | FROM_HEX(c->field[1]));
            (*c->received)++;
            c->fieldindex = 0;
        }
        break;
    }
}

int atnsorf_decode(int *sockfd, char *ip, int *port, uint8_t *data, size_t *received, size_t *remaining)
{
    struct nsorf_ctx ctx = {
        .sockfd = sockfd,
        .ip = ip,
        .port = port,
        .data = data,
        .remaining = remaining,
        .fieldno = 0,
        .fieldindex = 0,
        .dataidx = 0,
        .received = received,
    };
    return decode_input(CMD_TIMEOUT, &ctx, nsorf_char, nsorf_eol);
}

int atcpsms_decode()
{
    return decode_input(CMD_TIMEOUT, NULL, NULL, NULL);
}

int at_decode()
{
    return decode_input(CMD_TIMEOUT, NULL, NULL, NULL);
}

int at_poll(int32_t timeout)
{
    return decode_input(CMD_TIMEOUT, NULL, NULL, NULL);
}