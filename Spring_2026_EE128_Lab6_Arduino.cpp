#include <SPI.h>

char buff[255];
volatile byte indx;
volatile boolean process;

/* Lab 6 Write-up Q1 latency marker:
 * Arduino digital pin 7 goes HIGH when '\n' is received inside the SPI ISR.
 * Connect D7 to oscilloscope CH2.
 */
#define ARDUINO_END_PIN 7

static inline void pulse_arduino_end_marker(void)
{
   volatile byte i;

   /* Digital pin 7 = PD7 on Arduino Uno. Use direct port access inside ISR. */
   PORTD |= _BV(PORTD7);      // marker HIGH

   /* Short visible pulse delay. This is safe because '\n' is the last byte. */
   for (i = 0; i < 80; i++) {
      asm volatile ("nop");
   }

   PORTD &= ~_BV(PORTD7);     // marker LOW
}

void setup(void) {
   Serial.begin(115200);

   pinMode(MISO, OUTPUT);       // Arduino sends on MISO when acting as SPI slave
   pinMode(ARDUINO_END_PIN, OUTPUT);
   digitalWrite(ARDUINO_END_PIN, LOW);

   SPCR |= _BV(SPE);            // turn on SPI in slave mode

   indx = 0;                    // buffer empty
   process = false;

   SPI.attachInterrupt();       // turn on SPI interrupt
}

ISR(SPI_STC_vect) // SPI interrupt routine
{
   byte c = SPDR; // read byte from SPI Data Register

   if (indx < (sizeof(buff) - 1)) {
      buff[indx++] = c; // save data in the next index in the array buff

      if (c == '\n') {
         buff[indx - 1] = 0;     // replace newline with string terminator
         process = true;

         /* End marker: Arduino has detected the newline at the end of the SPI string. */
         pulse_arduino_end_marker();
      }
   } else {
      /* Buffer overflow protection: reset and wait for the next message. */
      indx = 0;
      process = false;
   }
}

void loop(void) {
   if (process) {
      process = false;          // reset the process flag
      Serial.println(buff);     // print the received string on serial monitor
      indx = 0;                 // reset buffer index
   }
}
