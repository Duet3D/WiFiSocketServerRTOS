#pragma once

class EEPROMClass {
public:
  EEPROMClass(uint32_t sector);
  EEPROMClass(void);

  void begin(size_t size);
//   uint8_t read(int address);
//   void write(int address, uint8_t val);
  bool commit();
//   void end();

//   uint8_t * getDataPtr();

  template<typename T>
#if 1	// DC
  T &get(int address, T &t) const {
#else
	  T &get(int address, T &t) {
#endif
    return t;
  }

#if 1	// DC added
	template<typename T> const T *getPtr(int address) const
	{
        return nullptr;
	}
#endif

  template<typename T> 
  const T &put(int address, const T &t) {
    return t;
  }

// protected:
//   uint32_t _sector;
//   uint8_t* _data;
//   size_t _size;
//   bool _dirty;
};

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_EEPROM)
extern EEPROMClass EEPROM;
#endif

#define SPI_FLASH_SEC_SIZE      4096

uint32 spi_flash_get_id(void);