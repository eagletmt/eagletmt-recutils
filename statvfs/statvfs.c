/*
 * Print total disk space and available disk space (in bytes) of the given
 * path. It's like df(1) but doesn't display any extra information for
 * machine-readability.
 */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/statvfs.h>

void print_statvfs(const char *path)
{
  struct statvfs buf;
  if (statvfs(path, &buf) == 0) {
    printf("%lu %lu\n",
        (buf.f_bsize) * buf.f_blocks,
        buf.f_bsize * buf.f_bavail);
  } else {
    exit(errno);
  }
}

int main(int argc, char *argv[])
{
  int i;
  for (i = 1; i < argc; i++) {
    print_statvfs(argv[i]);
  }
  return 0;
}
