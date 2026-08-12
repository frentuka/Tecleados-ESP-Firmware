#pragma once

typedef void (*button_callback_t)(void);

void button_init(button_callback_t on_single_press, button_callback_t on_double_press, button_callback_t on_hold);