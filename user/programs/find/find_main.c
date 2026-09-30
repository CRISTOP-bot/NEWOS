#include "../../lib/nshlib.h"

#define FIND_MAX_DEPTH 48
#define FIND_DIRENT_MAX 64

static const char *name_pattern;
static int type_filter;
static int min_depth;
static int max_depth = FIND_MAX_DEPTH;

static int name_matches(const char *name, const char *pattern)
{
    const char *star = 0;
    const char *retry = 0;

    while (*name) {
        if (*pattern == '?' || *pattern == *name) {
            name++;
            pattern++;
        } else if (*pattern == '*') {
            star = pattern++;
            retry = name;
        } else if (star) {
            pattern = star + 1;
            name = ++retry;
        } else {
            return 0;
        }
    }
    while (*pattern == '*')
        pattern++;
    return *pattern == '\0';
}

static int append_component(char *path, const char *name)
{
    u64 len = nstrlen(path);
    u64 nlen = nstrlen(name);
    u64 end = len;

    while (end > 1 && path[end - 1] == '/')
        end--;
    if (!end) {
        path[0] = '.';
        path[1] = '\0';
        end = 1;
    }
    if (end + (path[end - 1] == '/' ? 0u : 1u) + nlen >= NSH_MAX_PATH)
        return -1;
    path[end] = '\0';
    if (path[end - 1] != '/')
        path[end++] = '/';
    nmemcpy(path + end, name, nlen + 1);
    return 0;
}

static int path_name_matches(const char *path, const char *pattern)
{
    char name[NSH_MAX_PATH];
    u64 end = nstrlen(path);
    u64 start;
    while (end > 1 && path[end - 1] == '/')
        end--;
    start = end;
    while (start && path[start - 1] != '/')
        start--;
    if (end == 1 && path[0] == '/')
        start = 0;
    u64 len = end - start;
    if (!len) {
        name[0] = '/';
        name[1] = '\0';
    } else {
        nmemcpy(name, path + start, len);
        name[len] = '\0';
    }
    return name_matches(name, pattern);
}

static int walk(const char *path, int depth)
{
    struct nsh_dirent entries[FIND_DIRENT_MAX];
    struct nsh_stat st;
    long count;
    int errors = 0;
    int is_dir;

    if (nstrlen(path) >= NSH_MAX_PATH) {
        fputf(1, "find: %s: path too long\n", path);
        return 1;
    }
    if (sys_stat(path, &st) != 0) {
        fputf(1, "find: %s: path not found\n", path);
        return 1;
    }
    is_dir = !!(st.st_mode & MODE_DIR);
    if (depth >= min_depth &&
        (!name_pattern || path_name_matches(path, name_pattern)) &&
        (!type_filter || (type_filter == 'd' && is_dir) ||
         (type_filter == 'f' && (st.st_mode & MODE_REG))))
        putf("%s\n", path);
    if (!is_dir || depth >= max_depth)
        return 0;

    count = sys_readdir(path, entries, sizeof(entries));
    if (count < 0) {
        fputf(1, "find: %s: cannot read directory\n", path);
        return 1;
    }
    if (count > FIND_DIRENT_MAX)
        count = FIND_DIRENT_MAX;

    for (long i = 0; i < count; i++) {
        char child[NSH_MAX_PATH];
        int child_is_dir = !!(entries[i].mode & MODE_DIR);
        if (!entries[i].name[0] || nstrcmp(entries[i].name, ".") == 0 ||
            nstrcmp(entries[i].name, "..") == 0)
            continue;

        nstrcpy(child, path);
        if (append_component(child, entries[i].name) != 0) {
            fputf(1, "find: path too long below %s\n", path);
            errors = 1;
            continue;
        }

        if (child_is_dir)
            errors |= walk(child, depth + 1);
        else if (!child_is_dir && depth + 1 >= min_depth &&
                 (!name_pattern || name_matches(entries[i].name, name_pattern)) &&
                 (!type_filter || type_filter == 'f'))
            putf("%s\n", child);
    }
    return errors;
}

static int parse_depth(const char *s)
{
    int value = 0;
    if (!*s)
        return -1;
    while (*s) {
        if (*s < '0' || *s > '9')
            return -1;
        int digit = *s++ - '0';
        if (value > (FIND_MAX_DEPTH - digit) / 10)
            return -1;
        value = value * 10 + digit;
    }
    return value;
}

int main(int argc, char **argv)
{
    const char *roots[NSH_MAX_PATH];
    int root_count = 0;
    int expression = 0;
    int rc = 0;

    if (argc == 2 && nstrcmp(argv[1], "--help") == 0) {
        nputs("usage: find [path ...] [-name pattern] [-type f|d]\n");
        nputs("           [-mindepth n] [-maxdepth n]\n");
        return 0;
    }

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (!expression && arg[0] == '-')
            expression = 1;
        if (!expression) {
            if (root_count >= (int)(sizeof(roots) / sizeof(roots[0])))
                return fail("find", "too many starting paths");
            roots[root_count++] = arg;
            continue;
        }
        if (nstrcmp(arg, "-name") == 0) {
            if (++i >= argc || name_pattern)
                return fail("find", "-name requires one pattern");
            name_pattern = argv[i];
        } else if (nstrcmp(arg, "-type") == 0) {
            if (++i >= argc || type_filter)
                return fail("find", "-type requires f or d");
            if (nstrcmp(argv[i], "f") == 0)
                type_filter = 'f';
            else if (nstrcmp(argv[i], "d") == 0)
                type_filter = 'd';
            else
                return fail("find", "-type supports f or d");
        } else if (nstrcmp(arg, "-maxdepth") == 0) {
            if (++i >= argc)
                return fail("find", "-maxdepth requires a number");
            max_depth = parse_depth(argv[i]);
            if (max_depth < 0)
                return fail("find", "-maxdepth must be between 0 and 48");
        } else if (nstrcmp(arg, "-mindepth") == 0) {
            if (++i >= argc)
                return fail("find", "-mindepth requires a number");
            min_depth = parse_depth(argv[i]);
            if (min_depth < 0)
                return fail("find", "-mindepth must be between 0 and 48");
        } else {
            return fail("find", "usage: find [path ...] [-name pattern] [-type f|d]");
        }
    }
    if (min_depth > max_depth)
        return fail("find", "-mindepth exceeds -maxdepth");
    if (!root_count)
        roots[root_count++] = ".";
    for (int i = 0; i < root_count; i++)
        rc |= walk(roots[i], 0);
    return rc;
}
