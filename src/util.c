#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <xdwayland-client.h>

void __log(const char *filename, const char *level, const char *message, ...) {
  va_list args;
  va_start(args, message);
  printf("%s: %s: ", filename, level);
  vprintf(message, args);
  va_end(args);
}

void __print_error(const char *filename, const char *message, ...) {
  va_list args;
  va_start(args, message);
  fprintf(stderr, "%s: ERROR: ", filename);
  vfprintf(stderr, message, args);
  va_end(args);
}

int is_binary(const char *data, size_t data_size) {
  for (size_t i = 0; i < data_size; i++) {
    uint8_t ch = data[i];
    if (ch < 32 || ch > 126)
      return 1;
  }

  return 0;
}
