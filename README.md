# SYMB128


This sketch is intended to be an MB128 emulator

------------------------
***   NOTES ON USE   ***
------------------------

Using an Adafruit Itsy Bitsy M4 Express, connect:
- The Itsy Bitsy Ground pin to the common ground of the PC Engine
- Pin D9 to the 3.3V-level-shifted 'CLR' signal of the joypad
- Pin D10 to the 3.3V-level-shifted 'SEL' signal of the joypad
- Pin D11 to the 3.3V-level-shifted 'D0' signal of the joypad
- Pin D12 to the 3.3V-level-shifted 'D2' signal of the joypad
- Pin D13 to the 3.3V-level-shifted 'Proto-toggle' signal (switching between MB128 and joypad)

- Also connect the 3.3V-level-shifted 'D1' and 'D3' signals of the joypad to ground

- In order to persist data (store it in Flash memory), connect a pushbutton switch
  (normally open, momentary-contact) to pin D7 of the Itsy Bitsy, so that when the
  switch is pushed, D7 is brought 'LOW', or to a ground state


In order to setup the Itsy Bitsy M4 in the Arduino environment, see this page:
  https://learn.adafruit.com/introducing-adafruit-itsybitsy-m4/setup

In addition, you will need to download the Adafruit SPI, QSPI_Flash, and DotStar libraries
(and any additional dependencies they my have).

I recommend that you ensure that you can get the 'blink' sketch running on the M4 before trying
to run this sketch.

Run this sketch on the M4 @ 120MHz, with cache enabled.
Compiling wth fast option ('-O2') is optional but recommended
Leave SPI and QSPI settings at default


---------------------------------------
** Known issues/planned enhancements **
---------------------------------------

1) If the pushbutton is pressed in the middle of data transmission from the PC Engine,
   the file saved may be incomplete/corrupted, and the data transmission in progress
   will be interrupted/lost.  (** probably fixed, by checking state to be == STATE_IDLE **)

2) The 2MB SPI Flash memory can hold many 'slots' of MB128 data, but currently only one is
   being used.  A base address is currently roughed-in, but may be easy to put into use if a
   switch is added, bringing another GPIO pin to ground (or choosing among more than one)

3) Currently power draw is ~29mA in normal use, and peaks to ~40mA when saving to Flash
   memory.  Target power drain should be lower (~25mA).  One way to save power during peak
   loads is to drop the CPU clock while saving to Flash, and restoring speed when complete.
   MCLK->CPUDIV.reg = MCLK_CPUDIV_DIV(n) reduces clock, but n=1 is not full speed.
   However, DIV(4) drops to 16mA (plus Flash), and DIV(8) drops to 14mA (plus Flash)
   Need to find out how to bring it back to full speed, and test Flash write at low speed.

4) PC communication is via COM-over-USB at 500000 bits per second.  Currently using simple
   terminal programs, so dump/reload of memory should be improved with a custom system like
   a command-line, so that proper 8-bit communication can take place without interpreting it
   as control codes.  Currently, it is being converted to hex which is slow (and not as easy
   to handle as direct binary).

5) There is a trace log of commands, which can be queried over USB.  However, it has a limited
   depth (2048 MB128 commands), and there is no limit checking if more take place.  Be careful.

6) Adafruit updated their QSPI Flash libraries a week ago, and I had to work to get the system
   to recompile.  It may not be 100% working yet (example: eraseBlock() stopped working and I
   had to write a loop, using eraseSector() which is *supposed* to erase 4096 bytes at a time, but
   I did not fully test).  It is slower now - eraseBlock() would be nice to use if/when it becomes
   reliable again.
