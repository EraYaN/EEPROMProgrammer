#include <Arduino.h>
#define BLOCK_SIZE 128
#define EEPROM_SIZE 32000
// EEPROM size need to be multiple of BLOCKSIZE

enum State : uint8_t
{
    IDLE = 0,
    PROGRAMMING = 1,
    VERIFYING = 2,
    ERROR = 3,
    VERIFYING_WAITING_FOR_ACK = 4
};

enum CommandType : uint8_t
{
    START_PROGRAMMING = 0,
    ABORT = 1,
    START_VERIFYING = 2,
    PAYLOAD = 3,
    ACK = 4,
    DONE = 5,
};

// Both for sending and receiving
struct Command{
    CommandType type; // RX and TX
    State state; // TX only
    uint32_t address; // RX and TX
    uint8_t payload[BLOCK_SIZE]; // RX and TX
};

struct Config
{
    uint16_t block_size;
    uint16_t command_size;
    uint16_t type_offset;
    uint16_t state_offset;
    uint16_t address_offset;
    uint16_t payload_offset;
};

union ConfigUnion {
   Config config;
   byte buffer[sizeof(Config)];
};

union CommandUnion {
   Command command;
   byte buffer[sizeof(Command)];
};

State state = IDLE;

void setup()
{
    Serial.begin(115200);
    pinMode(13, OUTPUT); // LED
    digitalWrite(13, LOW);
    Serial.println("BOOT");
    Serial.println("EEPROM test title v1");

    ConfigUnion config = {
        .config = {
            .block_size = BLOCK_SIZE,
            .command_size = (uint16_t)sizeof(Command),
            .type_offset = (uint16_t)offsetof(Command, type),
            .state_offset = (uint16_t)offsetof(Command, state),
            .address_offset = (uint16_t)offsetof(Command, address),
            .payload_offset = (uint16_t)offsetof(Command, payload)
        }
    };

    Serial.println("STARTCONFIG");
    for(unsigned int i = 0; i < sizeof(Config); i++){
        Serial.write(config.buffer[i]);
    }

    // Extra processing logging is allowed
    delay(1000); // simulate slow booting

    //TODO init eeprom

    //This tells the PC to start.
    Serial.println("ENDBOOT");
}


uint32_t verify_address = 0;
unsigned int buf_pos = 0;
const int buffer_size = sizeof(Command);
byte buffer[buffer_size];

void write_command(CommandUnion cu){
    for(unsigned int i = 0; i < sizeof(Command); i++){
        Serial.write(cu.buffer[i]);
    }
}

void loop()
{
    if(buf_pos>=sizeof(Command) || state == VERIFYING){
        CommandUnion cu;
        memcpy(cu.buffer,buffer,sizeof(Command));
        buf_pos = 0;
        if(state==IDLE){
            if(cu.command.type==START_PROGRAMMING){
                state=PROGRAMMING;
                // SETUP Programming state here
                digitalWrite(13, HIGH);
            }
        } else if(state==PROGRAMMING){
            if(cu.command.type==PAYLOAD){
                digitalWrite(13, LOW);
                //handle payload here
                for(int i=0;i<BLOCK_SIZE;i++){
                    int addr = cu.command.address + i;
                    //TODO hookup eeprom
                    // address is 
                    // set EEPROM byte (addr,cu.command.payload[i]);
                }
                // Send ack
                CommandUnion ack_command;
                ack_command.command.type = ACK;
                ack_command.command.address = cu.command.address;

                memcpy(ack_command.command.payload, cu.command.payload, BLOCK_SIZE);
                
                write_command(ack_command);
            } else if(cu.command.type==START_VERIFYING){
                // SETUP Verifying state here
                verify_address = 0;
                digitalWrite(13, HIGH);
                state = VERIFYING;
            } else {
                digitalWrite(13, LOW);
                // Send abort
                CommandUnion abort_command;
                abort_command.command.type = ACK;
                abort_command.command.address = cu.command.address;

                memcpy(abort_command.command.payload, cu.command.payload, BLOCK_SIZE);
                
                write_command(abort_command);
            }
        } else if(state==VERIFYING){
            digitalWrite(13, HIGH);
            if(verify_address>=EEPROM_SIZE){
                // It is done
                CommandUnion done_command;
                done_command.command.type = DONE;
                done_command.command.address = verify_address;
                memset(done_command.command.payload, 0, BLOCK_SIZE);                
                write_command(done_command);
                state=IDLE;
            } else {
                CommandUnion verify_command;
                verify_command.command.type = PAYLOAD;
                verify_command.command.address = verify_address;
                for(int i=0;i<BLOCK_SIZE;i++){
                    //read from eeprom
                    //TODO hookup eeprom
                    verify_command.command.payload[i] = i; // DEMO data
                }
                write_command(verify_command);
                state = VERIFYING_WAITING_FOR_ACK;
            }
        
        } else if(state==VERIFYING_WAITING_FOR_ACK){
            if(cu.command.type==ACK){
                digitalWrite(13, LOW);
                verify_address+=BLOCK_SIZE;
                state=VERIFYING;
            } else if(cu.command.type==ABORT){
                // SETUP Verifying state here
                digitalWrite(13, LOW);
                state=IDLE;
            } else {
                digitalWrite(13, LOW);
                // Send abort
                CommandUnion abort_command;
                abort_command.command.type = ACK;
                abort_command.command.address = cu.command.address;

                memcpy(abort_command.command.payload, cu.command.payload, BLOCK_SIZE);
                
                write_command(abort_command);
            }
        }
    } else {
        while(Serial.available()>0){
            buffer[buf_pos++] = Serial.read();
            if(buf_pos>=sizeof(Command)){
                break;
            }
        }
    }
}
