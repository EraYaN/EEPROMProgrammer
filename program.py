import serial
import time
import random
import struct
import itertools
import collections

MAX_RETRIES = 2000

CONFIG_LENGTH = 12

config = {
    "BLOCK_SIZE": None,
    "COMMAND_SIZE": None,
    "TYPE_OFFSET": None,
    "STATE_OFFSET": None,
    "ADDRESS_OFFSET": None,
    "PAYLOAD_OFFSET": None,
    "COMMAND_FORMAT": None,
    "CONFIG_FORMAT": "=" + "H" * int(CONFIG_LENGTH / 2),
}

# Command Types
START_PROGRAMMING = 0
ABORT = 1
START_VERIFYING = 2
PAYLOAD = 3
ACK = 4
DONE = 5

# States
IDLE = 0
PROGRAMMING = 1
VERIFYING = 2
ERROR = 3
VERIFYING_WAITING_FOR_ACK = 4

# PC only states
BOOTING = 128
BOOTED = 129
READING_CONFIG = 130
FINISHING_BOOT = 131
PROGRAMMING_WAITING_FOR_ACK = 132
FINISHED = 255

state = BOOTING


class SliceableDeque(collections.deque):
    def __getitem__(self, s):
        try:
            start, stop, step = s.start or 0, s.stop or sys.maxsize, s.step or 1
        except AttributeError:  # not a slice but an int
            return super().__getitem__(s)
        try:  # normal slicing
            return list(itertools.islice(self, start, stop, step))
        except ValueError:  # incase of a negative slice object
            length = len(self)
            start, stop = (
                length + start if start < 0 else start,
                length + stop if stop < 0 else stop,
            )
            return list(itertools.islice(self, start, stop, step))


def peek_bytes_slice(dq, count):
    value = bytes(rx_buffer[0:count])
    return value


def peek_string_slice(dq, count):
    value = bytes(rx_buffer[0:count]).decode("ascii")
    return value


def advance(dq, count):
    discarded = []
    for _ in range(0, count):
        discarded.append(dq.popleft())


def string_insert(string, insert, start, end):
    return string[:start] + insert + string[end + 1 :]


rx_buffer = SliceableDeque()


def process_rx(input):
    rx_buffer.extend(input)
    #print("Section: ", input)
    #print("Buffer:", bytes(rx_buffer[0:4]))

file_contents = random.randbytes(32000)
file_pos = 0
verify_pos = 0

def process_buffer(ser, current_state):
    global file_pos
    global verify_pos
    global rx_buffer
    global file_contents
    global config
    new_state = current_state
    if len(rx_buffer) > 0 or state == PROGRAMMING:
        if state == BOOTING:
            #print("State BOOTING")
            buf = peek_string_slice(rx_buffer, 6)
            if buf == "BOOT\r\n":
                advance(rx_buffer, 6)
                new_state = BOOTED
            elif len(buf) >= 6:
                advance(rx_buffer, 1)  # allow garbage at the start
        elif state == BOOTED:
            #print("State BOOTED")
            buf = peek_string_slice(rx_buffer, 13)
            if buf == "STARTCONFIG\r\n":
                advance(rx_buffer, 13)
                new_state = READING_CONFIG
            elif len(buf) >= 13:
                advance(rx_buffer, 1)  # allow garbage like titles
        elif state == READING_CONFIG:
            #print("State READING_CONFIG")
            buf = peek_bytes_slice(rx_buffer, CONFIG_LENGTH)
            if len(buf) == CONFIG_LENGTH:
                output = struct.unpack(config["CONFIG_FORMAT"], buf)
                advance(rx_buffer, CONFIG_LENGTH)

                config["BLOCK_SIZE"] = output[0]
                config["COMMAND_SIZE"] = output[1]
                config["TYPE_OFFSET"] = output[2]
                config["STATE_OFFSET"] = output[3]
                config["ADDRESS_OFFSET"] = output[4]
                config["PAYLOAD_OFFSET"] = output[5]

                negoffset = 0  # becasue the format collapses
                format = "x" * config["COMMAND_SIZE"]
                format = string_insert(
                    format,
                    "B",
                    config["TYPE_OFFSET"] + negoffset,
                    config["TYPE_OFFSET"] + negoffset,
                )
                format = string_insert(
                    format,
                    "B",
                    config["STATE_OFFSET"] + negoffset,
                    config["STATE_OFFSET"] + negoffset,
                )
                format = string_insert(
                    format,
                    "I",
                    config["ADDRESS_OFFSET"] + negoffset,
                    config["ADDRESS_OFFSET"] + 3 + negoffset,
                )
                negoffset -= 3
                format = string_insert(
                    format,
                    f"{config['BLOCK_SIZE']}s",
                    config["PAYLOAD_OFFSET"] + negoffset,
                    config["PAYLOAD_OFFSET"] + config["BLOCK_SIZE"] -1 + negoffset,
                )
                config["COMMAND_FORMAT"] = "=" + format
                print("Got config", config)
                new_state = FINISHING_BOOT
        elif state == FINISHING_BOOT:
            #print("State FINISHING_BOOT")
            buf = peek_string_slice(rx_buffer, 9)
            if buf == "ENDBOOT\r\n":
                advance(rx_buffer, 9)
                command = struct.pack(config["COMMAND_FORMAT"],START_PROGRAMMING,state, file_pos, b'\x00' * config['BLOCK_SIZE'])
                ser.write(command)
                new_state = PROGRAMMING
            elif len(buf) >= 9:
                advance(rx_buffer, 1)  # allow garbage till the end of booting
            time.sleep(0.1)  # booting is slow wait for a bit
        elif state == PROGRAMMING:
            #print("State PROGRAMMING")
            if file_pos >= len(file_contents):
                print(f"Done programming at address {file_pos:06X}")
                command = struct.pack(config["COMMAND_FORMAT"],START_VERIFYING,state, file_pos, b'\x00' * config['BLOCK_SIZE'])
                ser.write(command)
                new_state=VERIFYING
            else:
                command = struct.pack(config["COMMAND_FORMAT"],PAYLOAD,state,file_pos,file_contents[file_pos:max(file_pos+config["BLOCK_SIZE"],len(file_contents))-1])
                if file_pos%4096==0:
                    print(f"TXD until {file_pos:06X}")
                ser.write(command)
                new_state = PROGRAMMING_WAITING_FOR_ACK
        elif state == PROGRAMMING_WAITING_FOR_ACK:
            #print("State PROGRAMMING_WAITING_FOR_ACK")
            buf = peek_bytes_slice(rx_buffer, config["COMMAND_SIZE"])
            if len(buf) == config["COMMAND_SIZE"]:
                advance(rx_buffer, config["COMMAND_SIZE"])
                #print(f"Parsing command: {buf}")
                output = struct.unpack(config["COMMAND_FORMAT"], buf)
                if output[0]==ACK:
                    if output[2] != file_pos:
                        raise ValueError(f"Address mismatch got {output[2]} expected {file_pos}")
                        
                    file_pos+=config["BLOCK_SIZE"]
                    if file_pos%4096==0:
                        print(f"ACK until {file_pos:06X}")
                    # handle output and verify
                    new_state = PROGRAMMING
                elif output[0]==ABORT:
                    new_state = ABORT
                    raise ValueError(f"Got abort message from programmer at address {output[2]}.")
        elif state == VERIFYING:
            #print("State VERIFYING", ser.out_waiting)
            buf = peek_bytes_slice(rx_buffer, config["COMMAND_SIZE"])
            if len(buf) == config["COMMAND_SIZE"]:
                advance(rx_buffer, config["COMMAND_SIZE"])
                #print(f"Parsing command: {buf}")
                output = struct.unpack(config["COMMAND_FORMAT"], buf)
                if output[0]==PAYLOAD:
                    if output[2] != verify_pos:
                        raise ValueError(f"Address mismatch got {output[2]} expected {verify_pos}")
                    
                    # handle output and verify
                    
                    # send ack 
                    command = struct.pack(config["COMMAND_FORMAT"],ACK,state,verify_pos,output[3])
                    ser.write(command)
                    verify_pos+=config["BLOCK_SIZE"]
                    if verify_pos%4096==0:
                        print(f"VER until {verify_pos:06X}")
                    new_state = VERIFYING
                    
                elif output[0]==DONE:
                    print(f"Done verifying at address {verify_pos:06X}")
                    # handle shutdown gracefully
                    new_state=FINISHED
                elif output[0]==ABORT:
                    new_state = ABORT
                    raise ValueError(f"Got abort message from programmer at address {output[2]}.")

    return new_state

start_time = time.time()
with serial.Serial("COM7", 115200, timeout=2) as ser:
    iter = 0
    
    while iter < 100:
        if ser.in_waiting > 0:
            process_rx(ser.read(ser.in_waiting))
        state = process_buffer(ser, state)
        if state==FINISHED:
            break
        iter += 0

end_time = time.time()

print(f"Completed in {end_time-start_time:.2f}")