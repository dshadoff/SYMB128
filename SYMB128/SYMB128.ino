#include <SPI.h>
#include <Adafruit_DotStar.h>
#include <Adafruit_QSPI_Flash.h>

Adafruit_QSPI_Flash flash;

//
// This sketch is intended to be an MB128 emulator
//
// ------------------------
// ***   NOTES ON USE   ***
// ------------------------
//
// Using an Adafruit Itsy Bitsy M4 Express, connect:
// - The Itsy Bitsy Ground pin to the common ground of the PC Engine
// - Pin D9 to the 3.3V-level-shifted 'CLR' signal of the joypad
// - Pin D10 to the 3.3V-level-shifted 'SEL' signal of the joypad
// - Pin D11 to the 3.3V-level-shifted 'D0' signal of the joypad
// - Pin D12 to the 3.3V-level-shifted 'D2' signal of the joypad
// - Pin D13 to the 3.3V-level-shifted 'Proto-toggle' signal (switching between MB128 and joypad)
//
// - Also connect the 3.3V-level-shifted 'D1' and 'D3' signals of the joypad to ground
//
// - In order to persist data (store it in Flash memory), connect a pushbutton switch
//   (normally open, momentary-contact) to pin D7 of the Itsy Bitsy, so that when the
//   switch is pushed, D7 is brought 'LOW', or to a ground state
//
//
// In order to setup the Itsy Bitsy M4 in the Arduino environment, see this page:
//   https://learn.adafruit.com/introducing-adafruit-itsybitsy-m4/setup
//
// In addition, you will need to download the Adafruit SPI, QSPI_Flash, and DotStar libraries
// (and any additional dependencies they my have).
//
// I recommend that you ensure that you can get the 'blink' sketch running on the M4 before trying
// to run this sketch.
//
// Run this sketch on the M4 @ 120MHz, with cache enabled.
// Compiling wth fast option ('-O2') is optional but recommended
// Leave SPI and QSPI settings at default
//
//
// ---------------------------------------
// ** Known issues/planned enhancements **
// ---------------------------------------
//
// 1) If the pushbutton is pressed in the middle of data transmission from the PC Engine,
//    the file saved may be incomplete/corrupted, and the data transmission in progress
//    will be interrupted/lost.  (** probably fixed, by checking state to be == STATE_IDLE **)
//
// 2) The 2MB SPI Flash memory can hold many 'slots' of MB128 data, but currently only one is
//    being used.  A base address is currently roughed-in, but may be easy to put into use if a
//    switch is added, bringing another GPIO pin to ground (or choosing among more than one)
//
// 3) Currently power draw is ~29mA in normal use, and peaks to ~40mA when saving to Flash
//    memory.  Target power drain should be lower (~25mA).  One way to save power during peak
//    loads is to drop the CPU clock while saving to Flash, and restoring speed when complete.
//    MCLK->CPUDIV.reg = MCLK_CPUDIV_DIV(n) reduces clock, but n=1 is not full speed.
//    However, DIV(4) drops to 16mA (plus Flash), and DIV(8) drops to 14mA (plus Flash)
//    Need to find out how to bring it back to full speed, and test Flash write at low speed.
//
// 4) PC communication is via COM-over-USB at 500000 bits per second.  Currently using simple
//    terminal programs, so dump/reload of memory should be improved with a custom system like
//    a command-line, so that proper 8-bit communication can take place without interpreting it
//    as control codes.  Currently, it is being converted to hex which is slow (and not as easy
//    to handle as direct binary).
//
// 5) There is a trace log of commands, which can be queried over USB.  However, it has a limited
//    depth (2048 MB128 commands), and there is no limit checking if more take place.  Be careful.
//
// 6) Adafruit updated their QSPI Flash libraries a week ago, and I had to work to get the system
//    to recompile.  It may not be 100% working yet (example: eraseBlock() stopped working and I
//    had to write a loop, using eraseSector() which is *supposed* to erase 4096 bytes at a time, but
//    I did not fully test).  It is slower now - eraseBlock() would be nice to use if/when it becomes
//    reliable again.


// ------------------------
// *** HARDWARE defines ***
// ------------------------
//
// This sketch is made specifically for the Adafruit Itsy Bitsy M4 Express
// We are using the pins marked as D09 and D10 for primary input
// These are the internal mappings for those external pin identifiers
// so that we can access them directly (...and 10 times faster !)

#define IN_PORT            REG_PORT_IN0
#define OUT_PORT_SET       REG_PORT_OUTSET0
#define OUT_PORT_CLR       REG_PORT_OUTCLR0


#define MB128_SAVETRIGGER  PORT_PA18    // Pin marked as D7 on Itsy Bitsy M4 Express; this will trigger save to Flash

#define MB128_CLK          PORT_PA19    // Pin marked as D09 on Itsy Bitsy M4 Express
#define MB128_DATAIN       PORT_PA20    // Pin marked as D10 on Itsy Bitsy M4 Express

// We are using the pins marked D11, D12 and D13 for output

#define MB128_DATAOUT      PORT_PA21    // Pin marked as D11 on Itsy Bitsy M4 Express
#define MB128_IDENTOUT     PORT_PA23    // Pin marked as D12 on Itsy Bitsy M4 Express
#define MB128_MBSELECTOUT  PORT_PA22    // Pin marked as D13 on Itsy Bitsy M4 Express; this also lights LED

#define NUMPIXELS 30 // Number of LEDs in strip
 
// Here's how to control the LEDs from any two pins:
#define DOTSTAR_NUMPIXELS  1
#define DOTSTAR_DATAPIN    8
#define DOTSTAR_CLOCKPIN   6

Adafruit_DotStar strip = Adafruit_DotStar(DOTSTAR_NUMPIXELS, DOTSTAR_DATAPIN, DOTSTAR_CLOCKPIN, DOTSTAR_BRG);

// --------------
// *** STATES ***
// --------------

// We also keep track of the system as a state machine, transitioning from one mode
// to the next.
//
// The following is a list of the various states which need to be uniquely identified

//
// STATE GROUP 1: Request Identification
//
#define STATE_IDLE          0     // set to joypad; disengaged
#define STATE_A8_A          1     // after initial 0xA8, first 2 cycles output

// STATE GROUP 2: Synced; request information
//
#define STATE_REQ           2     // '1' = READ; '0' = WRITE
#define STATE_ADDR          3     // 10 bits of address (lowest-significant bit signifies 128 bytes' offset)
#define STATE_LENBITS       4     // 3 bits (sub-byte length)
#define STATE_LENBYTES      5     // 17 bits (# of bytes in transfer)

// STATE GROUP 3: Synced; in-transfer states
//
#define STATE_READ          6     // byte-transfer portion of read command
#define STATE_READBITS      7     // leftover bit transfer portion of read command
#define STATE_READ_TRAIL    8     // block transfer complete; waiting for 3 trailing bits
#define STATE_WRITE         9     // byte-transfer portion of write command
#define STATE_WRITEBITS     10    // leftover bit transfer portion of write command
#define STATE_WRITE_TRAIL   11    // block transfer complete; waiting for confirmation + 3 trailing bits

#define STATE_ERR           20    // Error


#define WRITE_CMD   false
#define READ_CMD    true
bool read_write = READ_CMD;

// pin assignments in order to set them as inputs/outputs
//
const int savePin        =  7;
const int clockPin       =  9;
const int datainPin      = 10;
const int dataoutPin     = 11;
const int identoutPin    = 12;
const int mbselectoutPin = 13;


// Note:
// -----
// These are all globals so that they can be accessed as absolute memory addresses,
// without needing to do stackframe arithmetic, which would slow it down.
//
// Please be careful - do not use local variables in time-sensitive sections
//

char *hex = "0123456789ABCDEF";

int state = STATE_IDLE;

int shiftregin = 0;
int shiftreg_len = 0;

int shiftregout = 0;
long int count = 0;

long int old_read = 0;
long int new_read;
long int new_data;

bool input_bit;
int inbit;
int err = 0;
int data_change = 0;

char a;
int sector;
long int rw_index;
long int rw_count;
long int rw_bitcount;
int partialbits;
char tempval;
char writebitmask[] = {0xff, 0xfe, 0xfc, 0xf8, 0xf0, 0xe0, 0xc0, 0x80, 0x00};

int trace_state[2048];
long int trace_value[2048];
int trace_count = 0;

bool flash_present = false;
bool pending_write = false;

//
// This flag is to transition the output data value
// to high throughout the write cycle

bool writeout_onetime = false;

// Size and area where Memory Base 128 data is stored
//
#define MB128_SIZE        131072
#define ITSYM4_BLOCKSIZE  65536
#define ITSYM4_NUMSLOTS   16

int mem_slotnum = 0;

uint8_t array[MB128_SIZE];
char *array_ptr;

void setup() {

unsigned long int m_strt;
unsigned long int m_end;

  Serial.begin(115200);
  
  // put your setup code here, to run once:
  pinMode(savePin,        INPUT_PULLUP);
  pinMode(clockPin,       INPUT_PULLUP); 
  pinMode(datainPin,      INPUT_PULLUP);
  pinMode(dataoutPin,     OUTPUT);
  pinMode(identoutPin,    OUTPUT);
  pinMode(mbselectoutPin, OUTPUT);

  digitalWrite(dataoutPin,     LOW);
  digitalWrite(identoutPin,    LOW);
  digitalWrite(mbselectoutPin, LOW);

//
// This turns off the DotStar LED
//
  strip.begin();
  strip.show();
  
  strip.setPixelColor(0, 0, 0, 0);
  strip.show();

// bootup countdown complete

  delay(5000);  // wait for startup period - may be needed in case reprogramming needed -
                // especially if clock speed is wrong or other registers affected
                // in future, this delay can be set to occur only if a GPIO pin is set ("override")

  if (!flash.begin()){
    Serial.println("Could not find flash on QSPI bus!");  // should never happen on the correct board
    flash_present = false;
  }
  else {
    Serial.println("Found QSPI Flash");
    flash.readBuffer((mem_slotnum * MB128_SIZE), array, MB128_SIZE);
    flash_present = true;
  }

// At initialization time, there is no 'dirty cache', so we set the LED to
  pending_write = false;

  strip.setPixelColor(0, 50, 0, 0);
  strip.show();

// Note - measured time to initialize SRAM memory:
//
// Loop to clear:
//   Time was measured to be 11.1ms for 131071 elements or 84.5ns per element
//
// Read from QSPI flash:
//   Read from QSPI Flash is about 25ms - just over double the loop
//

// This is just here to display if you are watching the serial output
  Serial.println("Waiting");
  
  reset_state_to_idle();

  err = 0;
  old_read = 0;
  data_change = 0;
}

void reset_state_to_idle()
{
  OUT_PORT_CLR = (MB128_DATAOUT | MB128_IDENTOUT | MB128_MBSELECTOUT);
  interrupts();
  state = STATE_IDLE;
  if ((trace_count == 0) || (trace_state[trace_count-1] != STATE_IDLE)) {
    trace_state[trace_count] = STATE_IDLE;
    trace_value[trace_count++] = 0;
  }
  shiftregin = 0;
  shiftreg_len = 0;
  shiftregout = 0;
  rw_index = 0;
  rw_count = 0;
  rw_bitcount = 0;
}

// This is the state machine, which is the 'brains' of the MB128
// Note that the values of trace_state and trace_value are populated so that we can review
// the history of the MB128 commands without interfering too much with timing
//

void state_check()
{
  switch (state) {
    case STATE_IDLE:  // Generic state between commands and when disengaged

      if ((shiftregin == 0xA8) && (shiftreg_len > 7)) {
        // set 74HC157 to select MB128 side; otherwise, the following
        // response bits would not be sent

        OUT_PORT_SET = MB128_MBSELECTOUT;
        OUT_PORT_CLR = (MB128_DATAOUT | MB128_IDENTOUT);
        
// Note: I had to turn off Arduino interrupts, as the timer and timer overflow takes too much time
//       during a critical section.  They are turned back on when the system returns to an IDLE state

        noInterrupts();
        state = STATE_A8_A;
        trace_state[trace_count] = STATE_A8_A;
        trace_value[trace_count++] = 0;
        shiftregin = 0;
        shiftreg_len = 0;
      }
      break;
  
    case STATE_A8_A:  // State while responding to 0xA8 command
      if (input_bit) {
        OUT_PORT_SET = MB128_IDENTOUT;
        OUT_PORT_CLR = MB128_DATAOUT;
      }
      else {
        OUT_PORT_CLR = (MB128_DATAOUT | MB128_IDENTOUT);
      }

      if (shiftreg_len == 2) {
        state = STATE_REQ;
        trace_state[trace_count] = STATE_REQ;
        shiftregin = 0;
        shiftreg_len = 0;
      }
      break;
  
    case STATE_REQ:    // '1' for Read; '0' for Write
      read_write = input_bit;
      state = STATE_ADDR;
      trace_value[trace_count++] = input_bit;
      trace_state[trace_count] = STATE_ADDR;
      shiftregin = 0;
      shiftreg_len = 0;
      rw_index = 0;
      rw_count = 0;
      rw_bitcount = 0;
      break;

    case STATE_ADDR:    // 10 bits input; lowest bit still signifies 128 bytes of offset
      rw_index = rw_index >> 1;
      if (input_bit) {
        rw_index |= 0x10000;
      }
      else {
        rw_index &= 0x0FFFF;
      }
      if (shiftreg_len == 10) {
        state = STATE_LENBITS;
        trace_value[trace_count++] = rw_index;
        trace_state[trace_count] = STATE_LENBITS;
        shiftregin = 0;
        shiftreg_len = 0;
      }
      break;

    case STATE_LENBITS:    // 3 bits input (sub-byte-sized transfers)
      rw_bitcount = rw_bitcount >> 1;
      if (input_bit) {
        rw_bitcount |= 0x04;
      }
      else {
        rw_bitcount &= 0x03;
      }
      if (shiftreg_len == 3) {
        state = STATE_LENBYTES;
        trace_value[trace_count++] = rw_bitcount;
        trace_state[trace_count] = STATE_LENBYTES;
        shiftregin = 0;
        shiftreg_len = 0;
        partialbits = (8 - rw_bitcount);
      }
      break;

    case STATE_LENBYTES:    // 17 bits input
      rw_count = rw_count >> 1;
      if (input_bit) {
        rw_count |= 0x10000;
      }
      else {
        rw_count &= 0x0FFFF;
      }
      if (shiftreg_len == 17) {
        shiftregin = 0;
        shiftreg_len = 0;
        trace_value[trace_count++] = rw_count;

        if (read_write) {
          shiftregout = array[rw_index];
          if (rw_count == 0) {
            state = STATE_READBITS;
            trace_state[trace_count] = STATE_READBITS;
            trace_value[trace_count++] = 0;
            if (rw_bitcount == 0) {     // shoud never be 0 bytes + 0 bits
              rw_bitcount = 1;
              partialbits = 7;
            }
          }
          else {
            state = STATE_READ;
            trace_state[trace_count] = STATE_READ;
            trace_value[trace_count++] = 0;
          }
        }
        else {
          if (rw_count == 0) {
            state = STATE_WRITEBITS;
            trace_state[trace_count] = STATE_WRITEBITS;
            trace_value[trace_count++] = 0;
            if (rw_bitcount == 0) {     // shoud never be 0 bytes + 0 bits
              rw_bitcount = 1;
              partialbits = 7;
            }
          }
          else {
            state = STATE_WRITE;
            trace_state[trace_count] = STATE_WRITE;
            trace_value[trace_count++] = 0;
          }
        }
      }
      break;

     case STATE_READ:
      if (shiftregout & 0x01) {
        OUT_PORT_SET = MB128_DATAOUT;
      }
      else {
        OUT_PORT_CLR = MB128_DATAOUT;
      }

      if (shiftreg_len < 8) {
        shiftregout >>= 1;
      }
      else {
        rw_index++;
        shiftregout = array[rw_index];
        shiftreg_len = 0;
        rw_count--;
        if (rw_count == 0) {
          if (rw_bitcount == 0) {
            state = STATE_READ_TRAIL;
            trace_state[trace_count] = STATE_READ_TRAIL;
            trace_value[trace_count++] = 0;
          }
          else {
            shiftregout = array[rw_index];
            state = STATE_READBITS;
            trace_state[trace_count] = STATE_READBITS;
            trace_value[trace_count++] = 0;
          }
        }
      }
      break;

    case STATE_READBITS:
      if (shiftregout & 0x01) {
        OUT_PORT_SET = MB128_DATAOUT;
      }
      else {
        OUT_PORT_CLR = MB128_DATAOUT;
      }

      rw_bitcount--;
      if (rw_bitcount > 0) {
        shiftregout >>= 1;
      }
      else {
        shiftregin = 0;
        shiftreg_len = 0;
        state = STATE_READ_TRAIL;
        trace_state[trace_count] = STATE_READ_TRAIL;
        trace_value[trace_count++] = 0;
      }
      break;

    case STATE_READ_TRAIL:
      OUT_PORT_CLR = MB128_DATAOUT;    // end of sector; wait for 3 trailer bits
      if (shiftreg_len == 3) {
        reset_state_to_idle();
      }
      break;

    case STATE_WRITE:
      if (shiftreg_len == 8) {
        array[rw_index++] = shiftregin;
        shiftregin = 0;
        shiftreg_len = 0;
        rw_count--;

        if (rw_count == 0) {
          if (rw_bitcount == 0) {
            state = STATE_WRITE_TRAIL;
            trace_state[trace_count] = STATE_WRITE_TRAIL;
            trace_value[trace_count++] = 0;
          }
          else {
            tempval = array[rw_index] & writebitmask[partialbits];
            state = STATE_WRITEBITS;
            trace_state[trace_count] = STATE_WRITEBITS;
            trace_value[trace_count++] = 0;
          }
        }
      }
      break;

    case STATE_WRITEBITS:
      rw_bitcount--;
      if (rw_bitcount == 0) {
        array[rw_index] = tempval | (shiftregin >> partialbits);
        shiftregin = 0;
        shiftreg_len = 0;
        state = STATE_WRITE_TRAIL;
        trace_state[trace_count] = STATE_WRITE_TRAIL;
        trace_value[trace_count++] = 0;
      }
      break;

    case STATE_WRITE_TRAIL:
      if (shiftreg_len == 1) {             // end of sector; first two bits are {1, 0} confirmation
        OUT_PORT_SET = MB128_DATAOUT;
        OUT_PORT_CLR = MB128_IDENTOUT;
        pending_write = true;
      }
      else if (shiftreg_len == 2) {
        OUT_PORT_CLR = MB128_DATAOUT;
      }
      if (shiftreg_len == 5) {             // then, wait for 3 trailer bits
        reset_state_to_idle();

        strip.setPixelColor(0, 0, 50, 0);
        strip.show();
      }
      break;
  }
}

// Dump memory contents out to serial-over-USB
// (only first 5 sectors, for visual review)
//
void do_dump()
{
long int offset;
long int curr_sector;
int i, j;
unsigned char read_byte, disp_char;
char char_buf[24];

  for (curr_sector = 0; curr_sector < 5; curr_sector++) {
    Serial.print("Sector #");
    Serial.print(((curr_sector >> 4) & 0x0f), HEX);
    Serial.println((curr_sector & 0x0f), HEX);

    for (i = 0; i < 512; i+=16 ) {
      Serial.print(((i >> 8) & 0x0f), HEX);
      Serial.print(((i >> 4) & 0x0f), HEX);
      Serial.print((i & 0x0f), HEX);
      Serial.print(": ");
      for (j = 0; j < 16; j++) {

        offset = (curr_sector * 512) + i + j;
        read_byte = array[offset];

        if ((read_byte < 0x20) || (read_byte > 0x7f))
          disp_char = '.';
        else
          disp_char = read_byte;
          
        char_buf[j] = disp_char;

        Serial.print(((read_byte >> 4) & 0x0f), HEX);
        Serial.print((read_byte & 0x0f), HEX);
        Serial.print(" ");
      }
      for (j = 0; j < 16; j++) {
        Serial.print(char_buf[j]);        
      }
      Serial.println("");
    }
    Serial.println("");
  }
}

// Save (and load) complete memory contents out to serial-over-USB
//
void do_load()
{
long int block;
long int idx;
int read_byte;
uint8_t byte;

  // set to blue color to indicate that it is in the middle of programming
  //
  strip.setPixelColor(0, 0, 0, 50);
  strip.show();
/*
  for (block = 0; block < 256; block++) {
    for (idx = 0; idx < 512; idx++) {
      while (1) {
        if (Serial.available() > 0) {
          read_byte = Serial.read();

          if (read_byte != -1)
            break;
        }
      }
      byte = (uint8_t)read_byte;
      array[((block * 512) + idx)] = byte;
    }
  }
*/
  Serial.readBytes((char*)array, 512*256);
  Serial.flush();
  pending_write = true;
  
  strip.setPixelColor(0, 50, 0, 0);
  strip.show();
}

//// This old load function was based on HEX-printed-in-ASCII
//// and is now outmoded
////
//
//void do_load1()
//{
//long int block;
//long int idx;
//uint8_t read_byte, byte;
//
//  for (block = 0; block < 256; block++) {
//    for (idx = 0; idx < 512; idx++) {
//      byte = 0;
//      
//      //
//      // first digit
//      //
//      while (1) {
//        read_byte = Serial.read();
//        if ((read_byte >= '0') && (read_byte <= '9')) {
//          byte = (read_byte - '0') << 4;
//          break;
//        }
//        if ((read_byte >= 'A') && (read_byte <= 'F')) {
//          byte = (read_byte - 'A' + 10) << 4;
//          break;
//        }
//      }
//      
//      //
//      // second digit
//      //
//      while (1) {
//        read_byte = Serial.read();
//        if ((read_byte >= '0') && (read_byte <= '9')) {
//          byte += (read_byte - '0');
//          break;
//        }
//        if ((read_byte >= 'A') && (read_byte <= 'F')) {
//          byte += (read_byte - 'A' + 10);
//          break;
//        }
//      }
//      array[((block * 512) + idx)] = byte;
//    }
//  }
//  Serial.flush();
//  pending_write = true;
//  strip.setPixelColor(0, 0, 50, 0);
//  strip.show();
//}

void do_save()
{
long int block;
long int idx;
uint8_t read_byte;

  for (block = 0; block < 256; block++) {
    /*for (idx = 0; idx < 512; idx++) {
      read_byte = array[((block * 512) + idx)];
      Serial.write(read_byte);
    }*/
    Serial.write((char*)&array[block*512], 512);
    Serial.flush();
  }
}

// Dump trace log out over serial-over-USB:
//

void do_trace()
{
long int i;

  if (trace_count == 0) {
    Serial.println("No Trace Buffer");
    return;
  }

  Serial.println("Start");
  for (i = 0; i < trace_count; i++) {
    Serial.print("State = ");
    switch (trace_state[i]) {
      case STATE_IDLE:
        Serial.print("STATE_IDLE");
        Serial.print("; Value = ");
        Serial.println(trace_value[i]);
        break;

      case STATE_A8_A:
        Serial.print("STATE_A8_A");
        Serial.print("; Value = ");
        Serial.println(trace_value[i]);
        break;

      case STATE_REQ:
        Serial.print("STATE_REQ");
        Serial.print("; Value = ");
        if (trace_value[i] == 0) {
          Serial.println("0 (WRITE)");
        }
        else {
          Serial.println("1 (READ)");
        }
        break;

      case STATE_ADDR:
        Serial.print("STATE_ADDR");
        Serial.print("; Value = ");
        Serial.print(trace_value[i]);
        if ((trace_value[i] & 511) == 0) {
          Serial.print(" - Sector #");
          Serial.println((trace_value[i]/512));
        }
        break;

      case STATE_LENBITS:
        Serial.print("STATE_LENBITS");
        Serial.print("; Value = ");
        Serial.println(trace_value[i]);
        break;

      case STATE_LENBYTES:
        Serial.print("STATE_LENBYTES");
        Serial.print("; Value = ");
        Serial.println(trace_value[i]);
        break;

      case STATE_READ:
        Serial.print("STATE_READ");
        Serial.print("; Value = ");
        Serial.println(trace_value[i]);
        break;

      case STATE_READBITS:
        Serial.print("STATE_READBITS");
        Serial.print("; Value = ");
        Serial.println(trace_value[i]);
        break;

      case STATE_READ_TRAIL:
        Serial.print("STATE_READ_TRAIL");
        Serial.print("; Value = ");
        Serial.println(trace_value[i]);
        break;

      case STATE_WRITE:
        Serial.print("STATE_WRITE");
        Serial.print("; Value = ");
        Serial.println(trace_value[i]);
        break;

      case STATE_WRITEBITS:
        Serial.print("STATE_WRITEBITS");
        Serial.print("; Value = ");
        Serial.println(trace_value[i]);
        break;

      case STATE_WRITE_TRAIL:
        Serial.print("STATE_WRITE_TRAIL");
        Serial.print("; Value = ");
        Serial.println(trace_value[i]);
        break;

      case STATE_ERR:
        Serial.print("STATE_ERR");
        Serial.print("; Value = ");
        Serial.println(trace_value[i]);
        break;

      default:
        Serial.print("Unknown");
        Serial.print("; Value = ");
        Serial.println(trace_value[i]);
    }

  }
  Serial.println("End");
  trace_count = 0;
}

// Serial-over-USB interpreter:
//
void do_command()
{
char inChar;

  while (Serial.available()) {

    inChar = Serial.read();
    if ((inChar == 'D') || (inChar == 'd')) {
      do_dump();
    }
    else if ((inChar == 'L') || (inChar == 'l')) {
      do_load();
    }
//    else if ((inChar == 'M') || (inChar == 'm')) {
//      do_load1();
//    }
    else if ((inChar == 'S') || (inChar == 's')) {
      do_save();
    }
    else if ((inChar == 'T') || (inChar == 't')) {
      do_trace();
    }
    else {
      Serial.print("Bad command - Recieved: '");
      Serial.print(inChar);
      Serial.print("' - HEX ");
      Serial.println(inChar, HEX);
      Serial.println("D = Dump contents; S= Save; T = Trace");
    }
  }
}

void save_to_flash()
{
int i;
int sectorstart = (mem_slotnum * MB128_SIZE) / 4096;
int sectorend = ((mem_slotnum + 1) * MB128_SIZE) / 4096;
uint8_t tmp;

int blockstart = (mem_slotnum * MB128_SIZE) / 65536;
int blockend = ((mem_slotnum + 1) * MB128_SIZE) / 65536;

// Erase MUST take place first in order to set bits back to '1'
// If not erased, more bits will be brought to '0' (none will go to '1')

  // set to yellow while writing (was red if data was pending)
  strip.setPixelColor(0, 50, 50, 0);
  strip.show();

// Reduce CPU speed during flash write process, to reduce maximum current draw
//
// CPU current draw drops roughly 12-14mA; about equal to flash current draw
// A pleasant side effect is that flash erase/write/read are all faster when
// CPU runs slower, in some cases more than double
//
// Reduce speed by factor of 8 (DIV(1) = half), down to 15MHz:
//
  MCLK->CPUDIV.reg = MCLK_CPUDIV_DIV(4);

//  for (i = sectorstart; i < sectorend; i++) {
//    flash.eraseSector(i);
//  }
  for (i = blockstart; i < blockend; i++) {
    flash.eraseBlock(i);
  }
  flash.writeBuffer((mem_slotnum * MB128_SIZE), array, MB128_SIZE);

//
// Return CPU to speed from prior to the reduction
//
  MCLK->CPUDIV.reg = MCLK_CPUDIV_DIV_DIV1;

  // set to green once complete
  strip.setPixelColor(0, 50, 0, 0);
  strip.show();
}


// Here is where we monitor the signals.
// Critical section is inside the while(1) loop
// Timing is very important here and inside the state machine
//
void loop() {

  while (1) {
// *** this is here for testing ****
//  if (state == STATE_IDLE && pending_write && flash_present && ((IN_PORT & MB128_SAVETRIGGER) == 0)) {
//    save_to_flash();
//  }  
    new_read = (IN_PORT & (MB128_CLK | MB128_DATAIN));
    if (new_read != old_read)
      break;

    if (state == STATE_IDLE) {
      if (Serial.available()) {
        do_command();
      }

      if (((IN_PORT & MB128_SAVETRIGGER) == 0) && flash_present) {
        save_to_flash();
      }
    }
  }

  // First, check for clcok transitions, as they are the most time-dependent:
  //
  if ((new_read & MB128_CLK) && !(old_read & MB128_CLK)) // rising edge of CLK
  {
    data_change = 0;
    err = 0;
    if (new_read & MB128_DATAIN) {
      inbit = 0x80;
      input_bit = true;
    }
    else {
      inbit = 0;
      input_bit = false;
    }

    // Note that data is shifted in from the left, but shifted out from the right
    //
    shiftregin = (shiftregin >> 1) | inbit;
    shiftreg_len++;

    state_check();
  }

  else if (!(new_read & MB128_CLK) && (old_read & MB128_CLK)) // falling edge of CLK
  {
    data_change = 0;
  }

  // Now, check for DATA transitions, as they can violate the protocol:
  //
  // Data transition during CLK high - not allowed as part of MB128 protocol
  // (so it must belong to some other device; disengage)
  //
  if (((new_read & MB128_DATAIN) ^ (old_read & MB128_DATAIN)) && (new_data & MB128_CLK)) {
    err = 1;
    if (trace_state[trace_count-1] != STATE_IDLE) {
      trace_state[trace_count] = STATE_ERR;
      trace_value[trace_count++] = 1;
    }
    reset_state_to_idle();
  }

// Persist to flash.  This branch can only take place
// during a joypad scan.  The commented one above can take place at any time.
// Uncomment that one instead, if you have issues.
//
  if (state == STATE_IDLE && pending_write && ((IN_PORT & MB128_SAVETRIGGER) == 0) && flash_present) {
    save_to_flash();
  }  

//  I had originally considered that a data value transition more than once between clocks
//  (note: one or zero is normal) is an error state, which should abort a transfer-in-progress
//  It is, of course, normal during joypad polling when MB128 is not engaged.
//  However, Emerald Dragon actually requires this, so I have disabled the error code
//
//
  //  else if (((new_read & MB128_DATAIN) ^ (old_read & MB128_DATAIN)) && !(new_data & MB128_CLK))
  //  {
  //    // if this has already transitioned once during this clock, it is a violation
  //    // of the protocol (but normal for a joypad polling routine)
  //    
  //    data_change++;
  //    if (data_change > 1) {
  //      err = 2;
  //      if (trace_state[trace_count-1] != STATE_IDLE) {
  //        trace_state[trace_count] = STATE_ERR;
  //        trace_value[trace_count++] = 2;
  //      }
  //      reset_state_to_idle();
  //    }
  //  }
  
  old_read = new_read;
}
