#pragma once

#include "driver/gpio.h"
#include "esp8266/spi_register.h"

static const gpio_num_t SCK   = GPIO_NUM_14;
static const gpio_num_t MOSI  = GPIO_NUM_13;
static const gpio_num_t MISO  = GPIO_NUM_12;

#define REG(addr)   (*((volatile uint32_t *) addr))

#define SPI1C       REG(SPI_CTRL(1))
#define SPICWBO     SPI_WR_BIT_ORDER //SPI_WR_BIT_ODER
#define SPICRBO     SPI_RD_BIT_ORDER //SPI_RD_BIT_ODER
#define SPI1U       REG(SPI_USER(1))
#define SPI1U1      REG(SPI_USER1(1))
#define SPIUMOSI    SPI_USR_MOSI //MOSI phase, SPI_FLASH_DOUT
#define SPIUDUPLEX  BIT0 //SPI_DOUTDIN
#define SPILMOSI    SPI_USR_MOSI_BITLEN_S //9 bit in SPIxU1 default:0 (1bit)
#define SPILMISO    SPI_USR_MISO_BITLEN_S  //9 bit in SPIxU1 default:0 (1bit)
#define SPI1C1      REG(SPI_CTRL1(1))
#define SPI1S       REG(SPI_SLAVE(1))
#define SPIUSME     SPI_CK_OUT_EDGE //SPI Master Edge (0:falling, 1:rising), SPI_CK_OUT_EDGE
#define SPIUSSE     SPI_CK_I_EDGE //SPI Slave Edge (0:falling, 1:rising), SPI_CK_I_EDGE
#define SPI1P       REG(SPI_PIN(1))
#define SPI1CMD     REG(SPI_CMD(1))
#define SPIBUSY     SPI_USR //SPI_USR
#define GPMUX       REG(PERIPHS_IO_MUX)
#define SPI1CLK 	REG(SPI_CLOCK(1))
#define SPIMMISO	SPI_USR_MISO_BITLEN
#define SPIMMOSI	SPI_USR_MOSI_BITLEN
#define SPI1W0 		REG(SPI_W0(1))