#ifndef TEST_LOOP_H
#define TEST_LOOP_H

#include <stdint.h>
#include <stdbool.h>

void test_loop_start(uint32_t interval_ms);
void test_loop_stop(void);
bool test_loop_is_running(void);

#endif