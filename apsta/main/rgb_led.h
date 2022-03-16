/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "driver/rmt.h"
#include "led_strip.h"

void rgb_led_init();
void rgb_led_set(uint32_t r, uint32_t g, uint32_t b);