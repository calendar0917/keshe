#ifndef UTILS_H
#define UTILS_H

#include "protocol.h"
#include <pthread.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int path_validate(const char *path);
int path_resolve(const char *home, const char *path, char *resolved_out,
                 size_t out_size);
int log_msg(logger_t *logger, int level, const char *fmt, ...);

// 小端编码/解码
void uint64_to_le(uint64_t val, uint8_t out[8]);
uint64_t le_to_uint64(const uint8_t data[8]);

#endif // UTILS_H
