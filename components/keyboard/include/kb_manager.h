#pragma once

#include <stdint.h>

void kb_manager_set_paused(bool paused);

void kb_manager_start(void);

// NKRO test helper: send one key mapped from (row,col)
void kb_manager_test_nkro_keypress(uint8_t row, uint8_t col);