#include "shell.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

/* ===== output helpers ===== */

static void shell_write(shell_t *sh, const void *buf, size_t len) {
    if (sh->output)
        sh->output(buf, len);
}

static void shell_writes(shell_t *sh, const char *s) {
    shell_write(sh, s, strlen(s));
}

/* ===== terminal helpers ===== */

static void term_cursor_left(shell_t *sh, int n) {
    if (n <= 0) return;
    char buf[16];
    int len = 0;
    buf[len++] = '\033';
    buf[len++] = '[';
    if (n >= 100) { buf[len++] = (char)('0' + n / 100); n %= 100; }
    if (n >= 10)  { buf[len++] = (char)('0' + n / 10);  n %= 10;  }
    buf[len++] = (char)('0' + n);
    buf[len++] = 'D';
    shell_write(sh, buf, len);
}

static void term_cursor_right(shell_t *sh, int n) {
    if (n <= 0) return;
    char buf[16];
    int len = 0;
    buf[len++] = '\033';
    buf[len++] = '[';
    if (n >= 100) { buf[len++] = (char)('0' + n / 100); n %= 100; }
    if (n >= 10)  { buf[len++] = (char)('0' + n / 10);  n %= 10;  }
    buf[len++] = (char)('0' + n);
    buf[len++] = 'C';
    shell_write(sh, buf, len);
}

static void term_erase_to_end(shell_t *sh) {
    shell_writes(sh, "\033[K");
}

/* ===== built-in commands ===== */

static void cmd_echo(shell_t *sh, int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) shell_write(sh, " ", 1);
        shell_writes(sh, argv[i]);
    }
}

static void cmd_help(shell_t *sh, int argc, char **argv) {
    (void)argc; (void)argv;
    shell_writes(sh, "commands:\r\n");
    for (shell_cmd_t *c = sh->cmd_list; c; c = c->next) {
        shell_write(sh, "  ", 2);
        shell_writes(sh, c->name);
        shell_writes(sh, " - ");
        shell_writes(sh, c->help);
        if(c->next!=NULL)
        {
            shell_writes(sh, "\r\n");
        }
    }
}

/* ===== incremental line redraw ===== */

static void shell_redraw_tail(shell_t *sh, int from) {
    int n = sh->line_len - from;
    if (n > 0)
        shell_write(sh, &sh->line[from], n);
    term_erase_to_end(sh);

    int back = sh->line_len - sh->cursor_pos;
    if (back > 0)
        term_cursor_left(sh, back);
}

static void shell_sync_cursor(shell_t *sh, int old_pos) {
    int delta = sh->cursor_pos - old_pos;
    if (delta > 0)
        term_cursor_right(sh, delta);
    else if (delta < 0)
        term_cursor_left(sh, -delta);
}

/* ===== shell implementation ===== */

void shell_init(shell_t *sh, shell_output_fn output) {
    memset(sh, 0, sizeof(*sh));
    sh->output = output;
    ringbuf_init(&sh->rb);

    shell_register(sh, "echo", cmd_echo, "print arguments");
    shell_register(sh, "help", cmd_help, "show this help");
}

void shell_deinit(shell_t *sh) {
    shell_cmd_t *c = sh->cmd_list;
    while (c) {
        shell_cmd_t *next = c->next;
        SHELL_FREE(c);
        c = next;
    }
    sh->cmd_list = NULL;
}

bool shell_register(shell_t *sh, const char *name, shell_cmd_fn fn, const char *help) {
    shell_cmd_t *c = (shell_cmd_t *)SHELL_MALLOC(sizeof(shell_cmd_t));
    if (!c) return false;

    c->name = name;
    c->fn   = fn;
    c->help = help;
    c->next = sh->cmd_list;
    sh->cmd_list = c;
    return true;
}

static void show_prompt(shell_t *sh) {
    if (!sh->telnet_negotiated) {
        shell_writes(sh, "\xff\xfb\x01");
        sh->telnet_negotiated = true;
    }
    if (!sh->prompt_visible) {
        shell_writes(sh, "> ");
        sh->prompt_visible = true;
    }
}

static void execute_line(shell_t *sh) {
    shell_writes(sh, "\r\n");
    sh->prompt_visible = false;

    int    argc = 0;
    char  *argv[SHELL_ARGS_MAX];
    char  *p    = sh->line;
    bool   in_token = false;

    for (int i = 0; i < sh->line_len && p[i]; i++) {
        if (p[i] == ' ' || p[i] == '\t') {
            p[i] = '\0';
            in_token = false;
        } else {
            if (!in_token) {
                if (argc < SHELL_ARGS_MAX)
                    argv[argc++] = &p[i];
                in_token = true;
            }
        }
    }

    if (argc == 0) return;

    for (shell_cmd_t *c = sh->cmd_list; c; c = c->next) {
        if (strcmp(argv[0], c->name) == 0) {
            c->fn(sh, argc, argv);
            return;
        }
    }
    shell_writes(sh, argv[0]);
    shell_writes(sh, ": command not found\r");
}

/* ---- per-character handlers ---- */

static void shell_insert_char(shell_t *sh, char ch) {
    if (sh->line_len >= SHELL_LINE_MAX - 1) return;

    memmove(&sh->line[sh->cursor_pos + 1],
            &sh->line[sh->cursor_pos],
            sh->line_len - sh->cursor_pos);
    sh->line[sh->cursor_pos] = ch;
    sh->line_len++;
    sh->cursor_pos++;

    shell_write(sh, &ch, 1);
    int tail = sh->line_len - sh->cursor_pos;
    if (tail > 0) {
        shell_write(sh, &sh->line[sh->cursor_pos], tail);
        term_cursor_left(sh, tail);
    }
}

static void shell_backspace(shell_t *sh) {
    if (sh->cursor_pos == 0) return;

    memmove(&sh->line[sh->cursor_pos - 1],
            &sh->line[sh->cursor_pos],
            sh->line_len - sh->cursor_pos);
    sh->line_len--;
    sh->cursor_pos--;

    shell_writes(sh, "\b");
    shell_redraw_tail(sh, sh->cursor_pos);
}

static void shell_delete(shell_t *sh) {
    if (sh->cursor_pos >= sh->line_len) return;

    memmove(&sh->line[sh->cursor_pos],
            &sh->line[sh->cursor_pos + 1],
            sh->line_len - sh->cursor_pos - 1);
    sh->line_len--;

    shell_redraw_tail(sh, sh->cursor_pos);
}

static void shell_handle_esc(shell_t *sh, uint8_t key) {
    switch (key) {
    case 'K':  /* Left */
        if (sh->cursor_pos > 0) {
            int old = sh->cursor_pos;
            sh->cursor_pos--;
            shell_sync_cursor(sh, old);
        }
        break;
    case 'M':  /* Right */
        if (sh->cursor_pos < sh->line_len) {
            int old = sh->cursor_pos;
            sh->cursor_pos++;
            shell_sync_cursor(sh, old);
        }
        break;
    case 'S':  /* Delete */
        shell_delete(sh);
        break;
    case 'G':  /* Home */
        if (sh->cursor_pos > 0) {
            int old = sh->cursor_pos;
            sh->cursor_pos = 0;
            shell_sync_cursor(sh, old);
        }
        break;
    case 'O':  /* End */
        if (sh->cursor_pos < sh->line_len) {
            int old = sh->cursor_pos;
            sh->cursor_pos = sh->line_len;
            shell_sync_cursor(sh, old);
        }
        break;
    }
}

/* ---- main poll ---- */

bool shell_poll(shell_t *sh) {
    uint8_t ch;

    show_prompt(sh);

    while (ringbuf_get(&sh->rb, &ch)) {
        if (sh->esc_prefix != 0) {
            shell_handle_esc(sh, ch);
            sh->esc_prefix = 0;
            continue;
        }

        if (ch == 0xE0 || ch == 0x00) {
            sh->esc_prefix = (int)ch;
            continue;
        }

        if (ch == '\r' || ch == '\n') {
            sh->line[sh->line_len] = '\0';
            execute_line(sh);
            sh->line_len   = 0;
            sh->cursor_pos = 0;
        } else if (ch == 0x03) {
            shell_writes(sh, "^C\r\n");
            sh->line_len   = 0;
            sh->cursor_pos = 0;
            sh->prompt_visible = false;
        } else if (ch == 0x08 || ch == 0x7f) {
            shell_backspace(sh);
        } else if (ch >= 0x20 && ch < 0x7f) {
            shell_insert_char(sh, (char)ch);
        }
    }

    return false;
}
