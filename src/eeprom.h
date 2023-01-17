#include <Arduino.h>

#define EEPROM_SIZE 32768

#define PAGE_SIZE 64

// EEPROM size need to be multiple of BLOCKSIZE

#define E_WE 51
#define E_OE 52
#define E_CE 53

void SetPoortA_outputs();

void SetPoortA_inputs();

void writePage(uint32_t start_addr, uint8_t data[PAGE_SIZE]);


void writeValue(uint32_t adr, uint8_t val);

uint8_t readValue(uint32_t adr);