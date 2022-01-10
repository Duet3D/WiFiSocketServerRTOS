#pragma once

#include <stdint.h>

#define INPUT             0x00
#define SPECIAL           0xF8

#define CHANGE    0x03

#define ADC_MODE(mode) 

void attachInterrupt(uint8_t pin, void (*)(void), int mode);
void pinMode(uint8_t pin, uint8_t mode);
