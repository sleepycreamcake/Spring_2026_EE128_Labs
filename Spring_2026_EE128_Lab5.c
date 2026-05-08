#include "fsl_device_registers.h"

/*
 * DIP switch input:
 *
 * PTC0 -> ROT_DIR
 *        0 = CW
 *        1 = CCW
 *
 * PTC1 -> ROT_SPD
 *        0 = 22.5 degrees/second
 *        1 = 180 degrees/second
 */
#define ROT_DIR_MASK 0x01    // PTC0
#define ROT_SPD_MASK 0x02    // PTC1

/*
 * Stepper motor control through Port D:
 *
 * PTD0 -> IN1 -> A1 / Red
 * PTD1 -> IN2 -> A2 / Yellow
 * PTD2 -> IN3 -> B1 / Grey
 * PTD3 -> IN4 -> B2 / Green
 * PTD4 -> ENA
 * PTD5 -> ENB
 *
 * The lower 4 bits follow the full-step sequence:
 * Step0: 0110
 * Step1: 0101
 * Step2: 1001
 * Step3: 1010
 *
 * The upper two bits, PD4 and PD5, are both 1 to enable ENA and ENB.
 */
#define STEP0 0x36    // 0011 0110
#define STEP1 0x35    // 0011 0101
#define STEP2 0x39    // 0011 1001
#define STEP3 0x3A    // 0011 1010

/*
 * You measured that delay 100000 gives 22.5 degrees/second.
 *
 * 180 degrees/second is 8 times faster than 22.5 degrees/second.
 * Therefore:
 *
 * 100000 / 8 = 12500
 */
#define DELAY_22_5_DEG 100000UL
#define DELAY_180_DEG 12500UL

void software_delay(volatile unsigned long delay)
{
    while (delay != 0)
    {
        delay--;
    }
}

int main(void)
{
    unsigned char switch_value;
    unsigned char step_index = 0;
    unsigned long step_delay;

    unsigned char step_table[4] = {
        STEP0,
        STEP1,
        STEP2,
        STEP3
    };

    /*
     * Enable Port C and Port D clock gate.
     *
     * Port C is used for DIP switch input.
     * Port D is used for L298N output.
     */
    SIM_SCGC5 |= SIM_SCGC5_PORTC_MASK;
    SIM_SCGC5 |= SIM_SCGC5_PORTD_MASK;

    /*
     * Configure PTC0 and PTC1 as GPIO.
     *
     * PTC0 = direction switch
     * PTC1 = speed switch
     *
     * 0x0003 selects pins 0 and 1.
     * 0x0100 configures them as GPIO.
     */
    PORTC_GPCLR = 0x00030100;

    /*
     * Configure PTD0-PTD5 as GPIO.
     *
     * PTD0-PTD3 = IN1-IN4
     * PTD4 = ENA
     * PTD5 = ENB
     *
     * 0x003F selects pins 0 through 5.
     * 0x0100 configures them as GPIO.
     */
    PORTD_GPCLR = 0x003F0100;

    /*
     * PTC0 and PTC1 are inputs.
     */
    GPIOC_PDDR &= ~(ROT_DIR_MASK | ROT_SPD_MASK);

    /*
     * PTD0-PTD5 are outputs.
     */
    GPIOD_PDDR |= 0x3F;

    while (1)
    {
        /*
         * Read switch values.
         *
         * Pull-down logic:
         * switch open  = 0
         * switch closed = 1
         */
        switch_value = GPIOC_PDIR & (ROT_DIR_MASK | ROT_SPD_MASK);

        /*
         * Select speed.
         *
         * PTC1 = 0 -> 22.5 degrees/second
         * PTC1 = 1 -> 180 degrees/second
         */
        if (switch_value & ROT_SPD_MASK)
        {
            step_delay = DELAY_180_DEG;
        }
        else
        {
            step_delay = DELAY_22_5_DEG;
        }

        /*
         * Output current step.
         */
        GPIOD_PDOR = step_table[step_index];

        /*
         * Wait before moving to next step.
         */
        software_delay(step_delay);

        /*
         * Select direction.
         *
         * PTC0 = 0 -> CW
         * PTC0 = 1 -> CCW
         *
         * CW:
         * STEP0 -> STEP1 -> STEP2 -> STEP3 -> STEP0
         *
         * CCW:
         * STEP0 -> STEP3 -> STEP2 -> STEP1 -> STEP0
         */
        if (switch_value & ROT_DIR_MASK)
        {
            /*
             * CCW: step_index decreases.
             */
            if (step_index == 0)
            {
                step_index = 3;
            }
            else
            {
                step_index--;
            }
        }
        else
        {
            /*
             * CW: step_index increases.
             */
            step_index++;

            if (step_index >= 4)
            {
                step_index = 0;
            }
        }
    }

    return 0;
}
