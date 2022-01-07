#pragma once

class EEPROMClass {
public:
  EEPROMClass(uint32_t sector);
  EEPROMClass(void);

  void begin(size_t size);
  bool commit();
  template<typename T>
  T &get(int address, T &t) const {
    return t;
  }

	template<typename T> const T *getPtr(int address) const
	{
        return nullptr;
	}

  template<typename T> 
  const T &put(int address, const T &t) {
    return t;
  }
};

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_EEPROM)
extern EEPROMClass EEPROM;
#endif

#define SPI_FLASH_SEC_SIZE      4096

uint32 spi_flash_get_id(void);