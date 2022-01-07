#pragma once

#include <stdint.h>

#define HIGH 0x1
#define LOW  0x0

#define INPUT             0x00
#define OUTPUT            0x01
#define SPECIAL           0xF8

#define CHANGE    0x03

#define ADC_MODE(mode) 

void digitalWrite(uint8_t pin, uint8_t val);
int digitalRead(uint8_t pin);
void attachInterrupt(uint8_t pin, void (*)(void), int mode);
void pinMode(uint8_t pin, uint8_t mode);
void delay(unsigned long);

extern "C" unsigned long millis()
{
    return 0;
}

void delayMicroseconds(unsigned int us);


class HardwareSerial {
    public:
        void begin(unsigned long baud) {}
        void setDebugOutput(bool);
};

extern HardwareSerial Serial;