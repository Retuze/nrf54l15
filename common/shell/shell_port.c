#include <rtthread.h>

int shell_port_init(const char *greeting, const char *prompt)
{
    if (greeting) {
        rt_kprintf("%s", greeting);
    }
    (void)prompt;
    return 0;
}
