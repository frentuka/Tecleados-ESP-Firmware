/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "kb_manager.h"

#include "button.h"
#include "rgb.h"
#include "cfgmod.h"
#include "usbmod.h"

#include "tinyusb.h"
#include "tinyusb_default_config.h"

#define TAG "MAIN"

enum ColorSet {
    Red, Green, Blue
};
enum ColorSet current_color = Red;

void single_press_test()
{
    ESP_LOGI(TAG, "test single press");
    char* chars = "Tecleados";

    kb_manager_set_paused(true);
    if (tud_mounted()) {
        while (*chars) {
            usb_send_char(*chars);
            chars++;
        }
    }
    kb_manager_set_paused(false);

    // // ensure key release
    // uint8_t no_keys[6] = { 0 };
    // tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, no_keys);
}

void double_press_test()
{
    ESP_LOGI(TAG, "test double press");
    kb_manager_test_nkro_keypress(3, 3);
}

static void init_procedure(void)
{
    button_init(*single_press_test, *double_press_test);
    usb_init();
    cfg_init();
    kb_manager_start();
}

void app_main(void)
{
    printf("Hello world!!! :D\n");
    init_procedure();
}