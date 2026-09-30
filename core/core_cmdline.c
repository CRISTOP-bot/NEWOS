#include <core/core_bootinfo.h>
#include <core/core_types.h>
#include <iru_string.h>

const char *g_boot_cmdline = "";

void boot_capture_cmdline(const char *cmd)
{
    if (!cmd)
        return;
    g_boot_cmdline = cmd;
}

int boot_cmdline_has(const char *needle)
{
    const char *hay = g_boot_cmdline;
    size_t n = strlen(needle);

    if (!hay || !n)
        return 0;
    while (*hay) {
        while (*hay && (*hay == ' ' || *hay == '\t'))
            hay++;
        if (!strncmp(hay, needle, n) &&
            (hay[n] == '\0' || hay[n] == ' ' || hay[n] == '\t')) {
            return 1;
        }
        while (*hay && *hay != ' ') {
            if (*hay == '\t')
                break;
            hay++;
        }
    }
    return 0;
}

void boot_cmdline_read_value(const char *key, char *out, size_t out_size)
{
    const char *hay = g_boot_cmdline;
    size_t klen = strlen(key);
    if (!hay || !klen || !out || out_size == 0)
        return;
    out[0] = '\0';
    while (*hay) {
        while (*hay && (*hay == ' ' || *hay == '\t'))
            hay++;
        if (!strncmp(hay, key, klen) &&
            (hay[klen] == '=' || hay[klen] == '\0' || hay[klen] == ' ' || hay[klen] == '\t')) {
            if (hay[klen] == '=') {
                hay += klen + 1;
                size_t i = 0;
                while (*hay && *hay != ' ' && *hay != '\t' && i < out_size - 1) {
                    out[i++] = *hay++;
                }
                out[i] = '\0';
                return;
            }
            return;
        }
        while (*hay && *hay != ' ') {
            if (*hay == '\t')
                break;
            hay++;
        }
    }
}