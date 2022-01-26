#pragma once

#include "driver/gpio.h"
#include "esp8266/spi_register.h"

static const gpio_num_t SCK = GPIO_NUM_14;
static const gpio_num_t MOSI = GPIO_NUM_13;
static const gpio_num_t MISO = GPIO_NUM_12;

#define HSPI		1
#define REG(addr)	(*((volatile uint32_t *) addr))

#define SPIUDUPLEX  BIT0 //SPI_DOUTDIN
