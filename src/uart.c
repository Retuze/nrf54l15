#include "uart.h"
#include "nrf.h"
#include <stdarg.h>

#define UART      NRF_UARTE20_S
#define TX_PORT   1u
#define TX_PIN    9u                 /* P1.09 -> SAMD11 USB serial */

/* EasyDMA source buffer (must live in RAM). */
static uint8_t tx_buf[256];

void uart_init(void)
{
    /* Idle-high TX pin via GPIO before the UARTE takes it over. */
    NRF_P1_S->PIN_CNF[TX_PIN] =
        (GPIO_PIN_CNF_DIR_Output       << GPIO_PIN_CNF_DIR_Pos) |
        (GPIO_PIN_CNF_INPUT_Disconnect << GPIO_PIN_CNF_INPUT_Pos);
    NRF_P1_S->OUTSET = (1u << TX_PIN);

    UART->PSEL.TXD = (TX_PORT << UARTE_PSEL_TXD_PORT_Pos) | TX_PIN;  /* connected */
    UART->BAUDRATE = UARTE_BAUDRATE_BAUDRATE_Baud115200;
    UART->ENABLE   = UARTE_ENABLE_ENABLE_Enabled;
}

void uart_write(const void *buf, uint32_t len)
{
    if (len == 0) {
        return;
    }
    if (UART->ENABLE != UARTE_ENABLE_ENABLE_Enabled) {
        return;   /* not initialized yet — avoid blocking on DMA that never ends */
    }
    if (len > sizeof(tx_buf)) {
        len = sizeof(tx_buf);
    }
    const uint8_t *s = (const uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++) {
        tx_buf[i] = s[i];
    }

    UART->DMA.TX.PTR    = (uint32_t)tx_buf;
    UART->DMA.TX.MAXCNT = len;
    UART->EVENTS_DMA.TX.END = 0;
    UART->TASKS_DMA.TX.START = 1;
    while (UART->EVENTS_DMA.TX.END == 0) {
    }
}

/* ------------------------------------------------------- tiny printf -- */

static char *put_u32(char *p, uint32_t v, uint32_t base, int width, char pad)
{
    char tmp[16];
    int n = 0;
    do {
        uint32_t d = v % base;
        tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= base;
    } while (v);
    while (n < width) {
        tmp[n++] = pad;
    }
    while (n--) {
        *p++ = tmp[n];
    }
    return p;
}

void uprintf(const char *fmt, ...)
{
    char out[256];
    char *p = out;
    char *end = out + sizeof(out) - 1;
    va_list ap;
    va_start(ap, fmt);

    while (*fmt && p < end) {
        if (*fmt != '%') {
            *p++ = *fmt++;
            continue;
        }
        fmt++;
        int width = 0;
        char pad = ' ';
        if (*fmt == '0') {
            pad = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt++ - '0');
        }
        switch (*fmt++) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            while (*s && p < end) {
                *p++ = *s++;
            }
            break;
        }
        case 'c':
            *p++ = (char)va_arg(ap, int);
            break;
        case 'd': {
            int32_t v = va_arg(ap, int32_t);
            if (v < 0) {
                *p++ = '-';
                v = -v;
            }
            p = put_u32(p, (uint32_t)v, 10, width, pad);
            break;
        }
        case 'u':
            p = put_u32(p, va_arg(ap, uint32_t), 10, width, pad);
            break;
        case 'x':
            p = put_u32(p, va_arg(ap, uint32_t), 16, width, pad);
            break;
        case '%':
            *p++ = '%';
            break;
        default:
            *p++ = '?';
            break;
        }
    }
    va_end(ap);
    uart_write(out, (uint32_t)(p - out));
}
