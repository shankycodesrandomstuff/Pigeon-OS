#pragma once

#include <stdbool.h>
#include <stdint.h>

void hal_init(void);
void hal_gpio_write(uint8_t pin, bool value);
bool hal_gpio_read(uint8_t pin);
void hal_delay_ms(uint32_t ms);
