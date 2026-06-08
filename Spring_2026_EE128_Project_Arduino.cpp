#include <SPI.h>

char buff[255];
char printBuff[255];

volatile byte indx;
volatile boolean process;

/*
 * Common cathode RGB LED pins.
 *
 * D11/PB3 and D13/PB5 are used by SPI, so do not use them for LED PWM.
 */
#define RED_PIN    6   // Arduino D6 / PD6 / PWM
#define GREEN_PIN  5   // Arduino D5 / PD5 / PWM
#define BLUE_PIN   3   // Arduino D3 / PD3 / PWM

#define ARDUINO_END_PIN 7

/*
 * Color calibration.
 *
 * Red is dim and blue is bright, so tune these later if needed.
 *
 * For raw testing, keep all at 255.
 * Later example:
 * RED_MAX_PWM   = 255
 * GREEN_MAX_PWM = 180
 * BLUE_MAX_PWM  = 80
 */
#define RED_MAX_PWM    255
#define GREEN_MAX_PWM  255
#define BLUE_MAX_PWM   255

/*
 * Set to 1 for low-rate Serial debug.
 * Set to 0 for maximum stability and no Serial spam.
 */
#define SERIAL_DEBUG 1
#define SERIAL_PRINT_INTERVAL_MS 500UL

int lastRedPercent = 100;
int lastGreenPercent = 100;
int lastBluePercent = 100;

unsigned long lastSerialPrintTime = 0;

static inline void pulse_arduino_end_marker(void)
{
   volatile byte i;

   PORTD |= _BV(PORTD7);

   for (i = 0; i < 80; i++) {
      asm volatile ("nop");
   }

   PORTD &= ~_BV(PORTD7);
}

int clampPercent(int value)
{
   if (value < 0) {
      return 0;
   }

   if (value > 100) {
      return 100;
   }

   return value;
}

int percentToPwm(int percent, int maxPwm)
{
   percent = clampPercent(percent);

   return (percent * maxPwm + 50) / 100;
}

void setRgbPercent(int redPercent, int greenPercent, int bluePercent)
{
   redPercent = clampPercent(redPercent);
   greenPercent = clampPercent(greenPercent);
   bluePercent = clampPercent(bluePercent);

   lastRedPercent = redPercent;
   lastGreenPercent = greenPercent;
   lastBluePercent = bluePercent;

   /*
    * Common cathode:
    * analogWrite 0   = off
    * analogWrite 255 = full brightness
    */
   analogWrite(RED_PIN, percentToPwm(redPercent, RED_MAX_PWM));
   analogWrite(GREEN_PIN, percentToPwm(greenPercent, GREEN_MAX_PWM));
   analogWrite(BLUE_PIN, percentToPwm(bluePercent, BLUE_MAX_PWM));
}

void setup(void)
{
   Serial.begin(9600);

   pinMode(RED_PIN, OUTPUT);
   pinMode(GREEN_PIN, OUTPUT);
   pinMode(BLUE_PIN, OUTPUT);

   setRgbPercent(100, 100, 100);

   /*
    * Receive-only SPI slave.
    * Keep MISO as INPUT to avoid sending 5V logic from Arduino back to K64F.
    */
   pinMode(MISO, INPUT);

   /*
    * Force SS / PB2 / D10 LOW so Arduino SPI slave stays active.
    */
   pinMode(SS, OUTPUT);
   digitalWrite(SS, LOW);

   pinMode(ARDUINO_END_PIN, OUTPUT);
   digitalWrite(ARDUINO_END_PIN, LOW);

   indx = 0;
   process = false;

   /*
    * Enable SPI in slave mode and enable SPI interrupt.
    */
   SPCR = _BV(SPE) | _BV(SPIE);

#if SERIAL_DEBUG
   Serial.println("Arduino RGB SPI slave ready.");
#endif
}

ISR(SPI_STC_vect)
{
   byte c = SPDR;

   /*
    * If a full message is waiting for loop() to process,
    * ignore extra incoming bytes to avoid corrupting the buffer.
    */
   if (process) {
      return;
   }

   if (c == '\r') {
      return;
   }

   if (c == '\n') {
      buff[indx] = '\0';
      process = true;

      pulse_arduino_end_marker();
      return;
   }

   if (indx < (sizeof(buff) - 1)) {
      buff[indx++] = (char)c;
   } else {
      /*
       * Buffer overflow protection.
       */
      indx = 0;
      process = false;
   }
}

void loop(void)
{
   if (process) {
      noInterrupts();

      for (byte i = 0; i < sizeof(buff); i++) {
         printBuff[i] = buff[i];

         if (buff[i] == '\0') {
            break;
         }
      }

      printBuff[sizeof(printBuff) - 1] = '\0';

      process = false;
      indx = 0;

      interrupts();

      int redPercent = 0;
      int greenPercent = 0;
      int bluePercent = 0;

      int parsed = sscanf(printBuff, "%d,%d,%d",
                          &redPercent,
                          &greenPercent,
                          &bluePercent);

      if (parsed == 3) {
         setRgbPercent(redPercent, greenPercent, bluePercent);
      }
   }

#if SERIAL_DEBUG
   /*
    * Low-rate Serial debug.
    * Avoid printing every SPI message because 9600 baud is slow.
    */
   if (millis() - lastSerialPrintTime >= SERIAL_PRINT_INTERVAL_MS) {
      lastSerialPrintTime = millis();

      Serial.print("RGB=");
      Serial.print(lastRedPercent);
      Serial.print(",");
      Serial.print(lastGreenPercent);
      Serial.print(",");
      Serial.println(lastBluePercent);
   }
#endif
}