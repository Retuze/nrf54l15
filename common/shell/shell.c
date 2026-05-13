#include "shell.h"
#include "rtt.h"

#include <rtthread.h>
#include <string.h>

#define SHELL_PROMPT    "~ $ "
#define SHELL_PROMPT_LEN (sizeof(SHELL_PROMPT) - 1)

/* ---- built-in commands ------------------------------------------------- */

static void shell_cmd_help(int argc, char *argv[])
{
    rt_kprintf("available commands:\n");
    rt_kprintf("  help\n");
}

static const shell_cmd_t g_shell_cmds[] = {
    {"help", "list commands", shell_cmd_help},
};

#define SHELL_CMD_COUNT (sizeof(g_shell_cmds) / sizeof(g_shell_cmds[0]))

/* ---- instance ---------------------------------------------------------- */

static struct {
    char line_buf[SHELL_LINE_BUF_SIZE];
    int  buf_len;
    int  cursor;
} g_sh;

/* ---- cursor helpers ---------------------------------------------------- */

/* CSI n D — move cursor left n columns */
static void shell_cursor_left(int n)
{
    if (n <= 0) return;
    char tmp[8];
    int len = rt_snprintf(tmp, sizeof(tmp), "\x1b[%dD", n);
    rtt_write(tmp, len);
}

/* redraw from cursor to end of line, then restore cursor */
static void shell_redraw_suffix(void)
{
    if (g_sh.cursor >= g_sh.buf_len) return;
    int suffix_len = g_sh.buf_len - g_sh.cursor;
    rtt_write(g_sh.line_buf + g_sh.cursor, suffix_len);
    shell_cursor_left(suffix_len);
}

/* full redraw: \r + prompt + line + clear-to-EOL + restore cursor */
static void shell_redraw_line(void)
{
    rtt_write("\r", 1);
    rtt_write(SHELL_PROMPT, SHELL_PROMPT_LEN);
    rtt_write(g_sh.line_buf, g_sh.buf_len);
    rtt_write("\x1b[K", 3);
    shell_cursor_left(g_sh.buf_len - g_sh.cursor);
}

/* ---- insert / delete at cursor ----------------------------------------- */

static void shell_insert_char(char c)
{
    if (g_sh.buf_len >= SHELL_LINE_BUF_SIZE - 1) return;

    for (int i = g_sh.buf_len; i > g_sh.cursor; i--)
        g_sh.line_buf[i] = g_sh.line_buf[i - 1];
    g_sh.line_buf[g_sh.cursor] = c;
    g_sh.buf_len++;
    g_sh.cursor++;

    shell_redraw_suffix();
}

static void shell_delete_at_cursor(void)
{
    if (g_sh.cursor >= g_sh.buf_len) return;

    for (int i = g_sh.cursor; i < g_sh.buf_len - 1; i++)
        g_sh.line_buf[i] = g_sh.line_buf[i + 1];
    g_sh.buf_len--;

    rtt_write(g_sh.line_buf + g_sh.cursor, g_sh.buf_len - g_sh.cursor);
    rtt_write(" ", 1);
    rtt_write("\x1b[1D", 4);
    shell_cursor_left(g_sh.buf_len - g_sh.cursor);
}

static void shell_backspace_at_cursor(void)
{
    if (g_sh.cursor <= 0) return;

    for (int i = g_sh.cursor - 1; i < g_sh.buf_len - 1; i++)
        g_sh.line_buf[i] = g_sh.line_buf[i + 1];
    g_sh.buf_len--;
    g_sh.cursor--;
    shell_redraw_line();
}

/* ---- command execution ------------------------------------------------- */

static void shell_execute(char *line)
{
    int argc = 0;
    char *argv[SHELL_MAX_ARGS];
    char *p = line;

    while (*p && argc < SHELL_MAX_ARGS) {
        while (*p == ' ') p++;
        if (*p == '\0') break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p == ' ') *p++ = '\0';
    }

    if (argc == 0) return;

    for (int i = 0; i < (int)SHELL_CMD_COUNT; i++) {
        if (strcmp(argv[0], g_shell_cmds[i].name) == 0) {
            g_shell_cmds[i].handler(argc, argv);
            return;
        }
    }

    rt_kprintf("unknown command: %s\n", argv[0]);
}

/* ---- public API -------------------------------------------------------- */

void shell_init(void)
{
    rtt_write(SHELL_PROMPT, SHELL_PROMPT_LEN);
}

void shell_poll(void)
{
    char c;
    while (rtt_read(&c, 1) > 0) {
        /* ---- escape sequences ---- */
        if (c == '\x1b') {
            char seq[4];
            int  slen = 0;

            while (slen < 3 && rtt_read(&seq[slen], 1) > 0)
                slen++;

            if (slen >= 1 && seq[0] == '[') {
                if (slen >= 2 && seq[1] == 'D') {       /* Left  */
                    if (g_sh.cursor > 0) {
                        g_sh.cursor--;
                        rtt_write("\x1b[1D", 4);
                    }
                } else if (slen >= 2 && seq[1] == 'C') {/* Right */
                    if (g_sh.cursor < g_sh.buf_len) {
                        g_sh.cursor++;
                        rtt_write("\x1b[1C", 4);
                    }
                } else if (slen >= 3 && seq[1] == '3' && seq[2] == '~') {
                    /* Delete */
                    shell_delete_at_cursor();
                }
            }
            continue;
        }

        /* ---- enter ---- */
        if (c == '\r' || c == '\n') {
            rtt_write("\n", 1);
            g_sh.line_buf[g_sh.buf_len] = '\0';
            shell_execute(g_sh.line_buf);
            g_sh.buf_len = 0;
            g_sh.cursor  = 0;
            rtt_write(SHELL_PROMPT, SHELL_PROMPT_LEN);
            continue;
        }

        /* ---- backspace (BS / DEL key) ---- */
        if (c == '\b' || c == '\x7f') {
            shell_backspace_at_cursor();
            continue;
        }

        /* ---- printable ---- */
        if (c >= ' ') {
            shell_insert_char(c);
        }
    }
}
