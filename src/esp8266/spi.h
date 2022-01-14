#pragma once

#include "driver/gpio.h"

static const gpio_num_t SCK   = GPIO_NUM_14;
static const gpio_num_t MOSI  = GPIO_NUM_13;
static const gpio_num_t MISO  = GPIO_NUM_12;

#define ESP8266_REG(addr) *((volatile uint32_t *)(0x60000000+(addr)))

#define SPI1C       ESP8266_REG(0x108)
#define SPICWBO     (1 << 26) //SPI_WR_BIT_ODER
#define SPICRBO     (1 << 25) //SPI_RD_BIT_ODER
#define SPI1U       ESP8266_REG(0x11C)
#define SPI1U1      ESP8266_REG(0x120)
#define SPIUMOSI    (1 << 27) //MOSI phase, SPI_FLASH_DOUT
#define SPIUDUPLEX  (1 << 0) //SPI_DOUTDIN
#define SPILMOSI    17 //9 bit in SPIxU1 default:0 (1bit)
#define SPILMISO    8  //9 bit in SPIxU1 default:0 (1bit)
#define SPI1C1      ESP8266_REG(0x10C)
#define SPI1S       ESP8266_REG(0x130)
#define SPIUSME     (1 << 7) //SPI Master Edge (0:falling, 1:rising), SPI_CK_OUT_EDGE
#define SPIUSSE     (1 << 6) //SPI Slave Edge (0:falling, 1:rising), SPI_CK_I_EDGE
#define SPI1P       ESP8266_REG(0x12C)
#define SPI1CMD     ESP8266_REG(0x100)
#define SPIBUSY     (1 << 18) //SPI_USR
#define GPMUX       ESP8266_REG(0x800)
#define SPI1CLK 	ESP8266_REG(0x118)
#define SPIMMISO	0x1FF
#define SPIMMOSI	0x1FF
#define SPI1W0 		ESP8266_REG(0x140)