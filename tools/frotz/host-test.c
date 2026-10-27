/* host-test.c - host driver for the NEWOS frotz port tests.
 *
 * Compiles the ported Frotz translation units against the host libc so
 * the Z-machine core + dumb driver can run a story straight from a
 * pipe/file. The /bin entry (frotz_main.c) stays NEWOS-only because of
 * nshlib.h; this file provides the same argc/argv handoff.
 *
 * Usage: dfrotz-host [frotz options] story.z3
 */

int frotz_main(int argc, char *argv[]);

int main(int argc, char **argv)
{
    return frotz_main(argc, argv);
}
