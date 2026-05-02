#include "fsl_device_registers.h"

/*
 * EE128 Lab 4 - K64F PWM Input Capture
 *
 * Arduino D3 / PD3 PWM -> voltage divider -> K64F PTC10 / FTM3_C6
 *
 * Two 1D7S display:
 *   Port D = tens digit
 *   Port C = ones digit
 *
 * Display wiring follows Lab 3:
 *   Port D: PTD0=A, PTD1=B, ..., PTD6=G, PTD7=DP
 *   Port C: PTC0=A, PTC1=B, ..., PTC5=F, PTC7=G, PTC8=DP
 *
 * PTC6 does not exist.
 * PTC10 is used as FTM3_C6 input.
 */

#define PORTD_SEG_MASK 0x000000FF
#define PORTC_SEG_MASK 0x000001BF

unsigned char DigitToPattern(int num)
{
    unsigned char pattern = 0x00;

    switch (num)
    {
    case 0: pattern = 0x3F; break; // A B C D E F
    case 1: pattern = 0x06; break; // B C
    case 2: pattern = 0x5B; break; // A B D E G
    case 3: pattern = 0x4F; break; // A B C D G
    case 4: pattern = 0x66; break; // B C F G
    case 5: pattern = 0x6D; break; // A C D F G
    case 6: pattern = 0x7D; break; // A C D E F G
    case 7: pattern = 0x07; break; // A B C
    case 8: pattern = 0x7F; break; // A B C D E F G
    case 9: pattern = 0x6F; break; // A B C D F G
    default: pattern = 0x00; break; // All OFF
    }

    return pattern;
}

/*
 * Change 7-segment pattern(A~G to bit0~bit6)
 * To Port C actual wiring:
 *   PTC0=A, PTC1=B, ..., PTC5=F, PTC7=G
 *
 * Notice that PTC6 does not exist.
 */
unsigned short map_to_portc(unsigned char pattern)
{
    return (unsigned short)((pattern & 0x3F) | ((pattern & 0x40) << 1));
}

/* Port D: PTD0=A, PTD1=B, ..., PTD6=G */
void DisplayDigit_D(int num)
{
    unsigned char pattern = DigitToPattern(num);
    GPIOD_PDOR = (GPIOD_PDOR & ~PORTD_SEG_MASK) | pattern;
}

/* Port C: PTC0=A, PTC1=B, ..., PTC5=F, PTC7=G */
void DisplayDigit_C(int num)
{
    unsigned short pattern_c = map_to_portc(DigitToPattern(num));
    GPIOC_PDOR = (GPIOC_PDOR & ~PORTC_SEG_MASK) | pattern_c;
}

/* Two 1D7S: Port D = 2nd digit, Port C = 1st digit */
void DisplayNumber2Digits(unsigned int num)
{
    unsigned int tens;
    unsigned int ones;

    if (num > 99)
    {
        num = 99;
    }

    tens = num / 10;
    ones = num % 10;

    DisplayDigit_D(tens);
    DisplayDigit_C(ones);
}

int main(void)
{
    unsigned int t1;
    unsigned int t2;
    unsigned int t3;

    unsigned int high_time;
    unsigned int period;
    unsigned int duty;

    /*
     * Display GPIO initialization, same style as Lab 3.
     */
    SIM_SCGC5 |= SIM_SCGC5_PORTD_MASK; // Enable Port D Clock Gate Control
    SIM_SCGC5 |= SIM_SCGC5_PORTC_MASK; // Enable Port C Clock Gate Control

    PORTD_GPCLR = 0x00FF0100; // Configures Pins 0-7 on Port D to be GPIO
    PORTC_GPCLR = 0x01BF0100; // Configures Pins 0-8 except PTC6 on Port C to be GPIO

    GPIOD_PDDR = 0x000000FF; // PTD[7:0] output mode
    GPIOC_PDDR = 0x000001BF; // PTC[8:7], PTC[5:0] output mode

    GPIOD_PDOR = 0x00; // Init as 0000 0000
    GPIOC_PDOR = 0x00; // Init as 0000 0000

    DisplayNumber2Digits(0);

    /*
     * FTM3_C6 input capture initialization.
     * This follows the lecture example.
     */
    SIM_SCGC3 |= SIM_SCGC3_FTM3_MASK; // FTM3 clock enable

    PORTC_PCR10 = 0x300;              // Port C Pin 10 as FTM3_CH6, ALT3

    FTM3_MODE = 0x5;                  // Enable FTM3 and disable write protection
    FTM3_MOD = 0xFFFF;                // Max counter value
    FTM3_SC = 0x0E;                   // System clock / 64

    while (1)
    {
        /*
         * Measure duty cycle using:
         *
         *   t1 = rising edge
         *   t2 = falling edge
         *   t3 = next rising edge
         *
         *   high_time = t2 - t1
         *   period    = t3 - t1
         *   duty      = high_time / period * 100
         */

        FTM3_C6SC = 0x04;                    // Rising edge capture
        while (!(FTM3_C6SC & 0x80));         // Wait for CHF
        FTM3_C6SC &= ~(1 << 7);              // Clear CHF
        t1 = FTM3_C6V;                       // First rising edge

        FTM3_C6SC = 0x08;                    // Falling edge capture
        while (!(FTM3_C6SC & 0x80));         // Wait for CHF
        FTM3_C6SC &= ~(1 << 7);              // Clear CHF
        t2 = FTM3_C6V;                       // Falling edge

        FTM3_C6SC = 0x04;                    // Rising edge capture
        while (!(FTM3_C6SC & 0x80));         // Wait for CHF
        FTM3_C6SC &= ~(1 << 7);              // Clear CHF
        t3 = FTM3_C6V;                       // Second rising edge

        /*
         * FTM3 counter is 16-bit.
         * & 0xFFFF handles counter wraparound.
         */
        high_time = (t2 - t1) & 0xFFFF;
        period = (t3 - t1) & 0xFFFF;

        if (period != 0)
        {
            duty = (high_time * 100) / period;

            if (duty > 99)
            {
                duty = 99;
            }

            DisplayNumber2Digits(duty);
        }
        else
        {
            DisplayNumber2Digits(0);
        }
    }

    return 0;
}
