#ifndef SHELL_H
#define SHELL_H

#include "ringbuf.h"
#include <stdbool.h>
#include <stddef.h>

#ifndef SHELL_MALLOC
#define SHELL_MALLOC(sz)  malloc(sz)
#endif
#ifndef SHELL_FREE
#define SHELL_FREE(ptr)   free(ptr)
#endif

#define SHELL_LINE_MAX  128
#define SHELL_ARGS_MAX  8

typedef struct shell_cmd shell_cmd_t;
typedef struct shell     shell_t;

typedef void (*shell_cmd_fn)(shell_t *sh, int argc, char **argv);
typedef void (*shell_output_fn)(const void *buf, size_t len);

struct shell_cmd {
    const char   *name;
    shell_cmd_fn  fn;
    const char   *help;
    shell_cmd_t  *next;
};

struct shell {
    shell_output_fn  output;
    ringbuf_t        rb;
    shell_cmd_t     *cmd_list;

    char  line[SHELL_LINE_MAX];
    int   line_len;
    int   cursor_pos;
    int   esc_prefix;

    bool  prompt_visible;
    bool  telnet_negotiated;
};

void shell_init(shell_t *sh, shell_output_fn output);
void shell_deinit(shell_t *sh);
bool shell_register(shell_t *sh, const char *name, shell_cmd_fn fn, const char *help);
bool shell_poll(shell_t *sh);

#endif
