#include "fsl_device_registers.h"

#define SW1_MASK 0x04    // PTB2
#define SW2_MASK 0x08    // PTB3

void software_delay(unsigned long delay)
{
    while (delay != 0) delay--;
}

unsigned short map_ledbar_to_portc(unsigned char logical)
{
    return (unsigned short)((logical & 0x3F) | ((logical & 0xC0) << 1));
}

int main(void)
{
    SIM_SCGC5 |= SIM_SCGC5_PORTD_MASK; // Enable Port D Clock Gate Control
    SIM_SCGC5 |= SIM_SCGC5_PORTC_MASK; // Enable Port C Clock Gate Control
    SIM_SCGC5 |= SIM_SCGC5_PORTB_MASK; // Enable Port B Clock Gate Control
    PORTD_GPCLR = 0x00FF0100; // Configures Pins 0-7 on Port D to be GPIO
    PORTC_GPCLR = 0x01BF0100; // Configures Pins 0-7 on Port C to be GPIO
    PORTB_GPCLR = 0x00FF0100; // Configures Pins 0-7 on Port B to be GPIO
    GPIOD_PDDR = 0x000000FF; // [7:0] Output mode
    GPIOC_PDDR = 0x000001BF; // [8:7], [5:0] Output mode
    GPIOB_PDDR = 0xFFFFFF00; // [7:0] Input mode
    GPIOD_PDOR = 0x01; // Init as 0000 0001
    GPIOC_PDOR = 0x01; // Init as 0000 0001
    GPIOB_PDOR = 0xFF; // Init as 1111 1111

    unsigned char counter = 0x01;
    GPIOC_PDOR = map_ledbar_to_portc(counter);

    while (1) {
        software_delay(4000000); // S/W Delay
        unsigned char current = (unsigned char)(GPIOD_PDOR & 0xFF);

        // Rotator
        if (GPIOB_PDIR & SW2_MASK) {
            if (current & 0x80) {    // When 1000 0000
                GPIOD_PDOR = 0x01;
            }
            else {
                GPIOD_PDOR = (current << 1);
            }
        }
        else {
            if (current & 0x01) {    // When 0000 0001
                GPIOD_PDOR = 0x80;
            }
            else {
                GPIOD_PDOR = (current >> 1);
            }
        }

        // Counter
        if (GPIOB_PDIR & SW1_MASK) {
            counter++;
            if (counter == 0x100) {
                counter = 0x01;
            }
            GPIOC_PDOR = map_ledbar_to_portc(counter);
        }
        else {
            counter--;
            if (counter == 0x00) {
                counter = 0xFF;
            }
            GPIOC_PDOR = map_ledbar_to_portc(counter);
        }

    }
    return 0;
}