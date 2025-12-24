#ifndef XDCLIP_UTIL_H
#define XDCLIP_UTIL_H

#include <stdio.h>
#include <xdwayland-client.h>

#define STREQ(s1, s2) strcmp((s1), (s2)) == 0

void __log(const char *filename, const char *level, const char *message, ...);
void __print_error(const char *filename, const char *message, ...);
int is_binary(const char *data, size_t data_size);

#endif
