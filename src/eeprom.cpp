#include "eeprom.h"




void SetPoortA_outputs() {
  digitalWrite(E_WE, HIGH);  // disable write mode
  digitalWrite(E_CE, HIGH);
  digitalWrite(E_OE, HIGH);
  DDRA = B11111111;        // set portA as outputs
  delayMicroseconds(1000);  // we need a little delay to switch from input to output (at least 500 microseconds)
}

void SetPoortA_inputs() {
  digitalWrite(E_WE, HIGH);  // disable write mode
  digitalWrite(E_CE, HIGH);
  digitalWrite(E_OE, HIGH);
  PORTA = 0;
  delay(1);
  DDRA = B00000000;        // set portA as Inputs
   delayMicroseconds(1000);  // we need a little delay to switch from input to output (at least 500 microseconds)

}

void writePage(uint32_t start_addr, uint8_t data[PAGE_SIZE]){
    // set up
    digitalWrite(E_OE, HIGH);
    digitalWrite(E_CE, LOW);

    delayMicroseconds(1);
    
    PORTL = (uint8_t)((start_addr >> 8) & 0xFF); // High byte (A6-A16)
    
    delayMicroseconds(1);

    for(int i = 0; i< PAGE_SIZE; i++){
        int addr = start_addr+i;
        //set address
        PORTC = (uint8_t)(addr & 0xFF);
        //set data
        PORTA = data[i];            // this is the DATA
        PORTB &= 0xfb; //digitalWrite(E_WE, LOW);
        PORTB |= 0x04; //digitalWrite(E_WE, HIGH);
        delayMicroseconds(5);
    }
    delayMicroseconds(10000);
}


void writeValue(uint32_t adr, uint8_t val) {
  digitalWrite(E_CE, LOW);
  digitalWrite(E_WE, HIGH);
  digitalWrite(E_OE, HIGH);  // output disable
  delayMicroseconds(10);

  // now put the address and data on the bus
  PORTA = val;            // this is the DATA
  PORTC = (uint8_t)(adr & 0xFF);   // lower byte of the address (first 8 bits)
  PORTL = (uint8_t)((adr >> 8) & 0xFF);  // upper byte of the address (only 5 bits are used, the address is a 13 bit number)
  delayMicroseconds(50000);


  digitalWrite(E_CE, LOW);  //  |CE goes LOW  // Chip enable
  digitalWrite(E_WE, LOW);  //  |WE goes LOW  // Write enable
  delayMicroseconds(10);
  digitalWrite(E_WE, HIGH);  //  |WE goes HIGH // Write disable
  digitalWrite(E_CE, HIGH);  //  |CE goes HIGH // Chip disable
  delayMicroseconds(10);
}

uint8_t readValue(uint32_t adr) {
  uint8_t r = 0;

  PORTC =  (uint8_t)(adr & 0xFF);   // lower byte of the address (first 8 bits)
  PORTL = (uint8_t)((adr >> 8) & 0xFF);  // upper byte of the address (only 5 bits are used, the address is a 13 bit number)

  digitalWrite(E_CE, LOW);
  digitalWrite(E_OE, LOW);
  delayMicroseconds(10);

  // read the data
  r = PINA;
  digitalWrite(E_CE, HIGH);
  digitalWrite(E_OE, HIGH);
  return r;
}