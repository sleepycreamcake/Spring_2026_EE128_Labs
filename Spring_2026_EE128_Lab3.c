#include "fsl_device_registers.h"

#define SW1_MASK 0x04   // PTB2
#define SW2_MASK 0x08   // PTB3

#define MODE_SW_MASK 0x04   // switch0: PTB2
#define CNT_DIR_MASK 0x08   // switch1: PTB3

#define PORTD_SEG_MASK 0x7F     // PTD0~PTD6 = A~G
#define PORTC_SEG_MASK 0xBF     // PTC0~PTC5, PTC7 = A~G (skip PTC6)

#define FG_CLK_MASK 0x02   // PTA1
volatile unsigned int counter = 0;

unsigned short ADC_read16b(void);
unsigned int ADC_to_99(unsigned short adc_value);
unsigned char DigitToPattern(int num);
unsigned short map_to_portc(unsigned char pattern);
void DisplayDigit(int num);            // Port D digit
void DisplayDigit_C(int num);          // Port C digit
void DisplayNumber2Digits(unsigned int num);

void software_delay(unsigned long delay)
{
	while (delay != 0) delay--;
}

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
 * To Port C actual wiring：
 * PTC0=A, PTC1=B, ..., PTC5=F, PTC7=G
 * Notice that PTC6 doesn't exists
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

/* Two 1D7S: Port D = 2rd digit，Port C = 1st digit*/
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

	DisplayDigit_D(tens);     // 2rd -> Port D
	DisplayDigit_C(ones);   // 1st -> Port C
}

unsigned short ADC_read16b(void)
{
	ADC0_SC1A = 0x00; // Start conversion from ADC0_DP0 / channel 0
	while (ADC0_SC2 & ADC_SC2_ADACT_MASK);          // Conversion in progress
	while (!(ADC0_SC1A & ADC_SC1_COCO_MASK));       // Wait until conversion completes
	return ADC0_RA;                                 // Return raw 16-bit ADC result
}

/* 16-bit ADC -> decimal 0~99 */
unsigned int ADC_to_99(unsigned short adc_value)
{
	unsigned int value;

	value = ((unsigned long)adc_value * 100) / 65536;

	if (value > 99)
	{
		value = 99;
	}

	return value;
}

/* PTA1 as external clock interrupt input */
void Init_PTA1_Interrupt(void)
{
	SIM_SCGC5 |= SIM_SCGC5_PORTA_MASK;   // Enable Port A clock

	PORTA_PCR1 = PORT_PCR_MUX(1) | PORT_PCR_IRQC(0xA);
	// MUX = 001 -> GPIO
	// IRQC = 1001 -> Interrupt on rising edge

	GPIOA_PDDR &= ~FG_CLK_MASK;          // PTA1 as input

	PORTA_ISFR = FG_CLK_MASK;            // Clear interrupt flag
	NVIC_ClearPendingIRQ(PORTA_IRQn);
	NVIC_EnableIRQ(PORTA_IRQn);
}

/* External clock interrupt handler for PTA1 */
void PORTA_IRQHandler(void)
{
	if (PORTA_ISFR & FG_CLK_MASK)
	{
		PORTA_ISFR = FG_CLK_MASK;    // Clear interrupt flag first

		/* Only count when MODE_SW == 1 (counter mode) */
		if ((GPIOB_PDIR & MODE_SW_MASK) == 0)
		{
			// Opposite because for pull-up, if the switch is OFF, it means 0 for switch;
			// but the MCU will receive voltage, which is 1.
			/* CNT_DIR == 1 : downward counting */
			if ((GPIOB_PDIR & CNT_DIR_MASK) == 0)
			{
				if (counter == 0)
				{
					counter = 99;
				}
				else
				{
					counter--;
				}
			}
			/* CNT_DIR == 0 : upward counting */
			else
			{

				if (counter >= 99)
				{
					counter = 0;
				}
				else
				{
					counter++;
				}
			}
		}
	}
}

int main(void)
{
	SIM_SCGC5 |= SIM_SCGC5_PORTD_MASK; // Enable Port D Clock Gate Control
	SIM_SCGC5 |= SIM_SCGC5_PORTC_MASK; // Enable Port C Clock Gate Control
	SIM_SCGC5 |= SIM_SCGC5_PORTB_MASK; // Enable Port B Clock Gate Control
	SIM_SCGC6 |= SIM_SCGC6_ADC0_MASK;  // Enable ADC0 Clock

	PORTD_GPCLR = 0x00FF0100; // Configures Pins 0-7 on Port D to be GPIO
	PORTC_GPCLR = 0x01BF0100; // Configures Pins 0-8 except PTC6 on Port C to be GPIO
	PORTB_GPCLR = 0x00FF0100; // Configures Pins 0-7 on Port B to be GPIO

	GPIOD_PDDR = 0x000000FF; // [7:0] Output mode
	GPIOC_PDDR = 0x000001BF; // [8:7], [5:0] Output mode
	GPIOB_PDDR = 0xFFFFFF00; // [7:0] Input mode

	GPIOD_PDOR = 0x00; // Init as 0000 0000
	GPIOC_PDOR = 0x00; // Init as 0000 0000
	GPIOB_PDOR = 0xFF; // Init as 1111 1111

	ADC0_CFG1 = 0x0C; // 16-bit ADC; Bus Clock
	ADC0_SC1A = 0x1F; // Disable module first, ADCH = 11111

	Init_PTA1_Interrupt();

	unsigned short adc_raw = 0;
	unsigned int adc_display = 0;

	while (1)
	{
		/* MODE_SW == 0 : ADC mode */
		if ((GPIOB_PDIR & MODE_SW_MASK) == 0)
		{
			DisplayNumber2Digits(counter);
		}
		/* MODE_SW == 1 : Counter mode */
		else
		{
			adc_raw = ADC_read16b();
			adc_display = ADC_to_99(adc_raw);
			DisplayNumber2Digits(adc_display);

		}

		software_delay(1000000); // S/W Delay
	}

	return 0;
}
