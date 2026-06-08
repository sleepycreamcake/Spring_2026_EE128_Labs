/* ###################################################################
 **     Filename    : main.c
 **     Project     : EE128_Final_Project / Lab_6
 **     Processor   : MK64FN1M0VLL12
 ** ###################################################################*/

#include "Cpu.h"
#include "Events.h"
#include "Pins1.h"
#include "FX1.h"
#include "GI2C1.h"
#include "WAIT1.h"
#include "CI2C1.h"
#include "CsIO1.h"
#include "IO1.h"
#include "MCUC1.h"
#include "SM1.h"

#include "PE_Types.h"
#include "PE_Error.h"
#include "PE_Const.h"
#include "IO_Map.h"
#include "PDD_Includes.h"
#include "Init_Config.h"

#include "MK64F12.h"
#include <stdio.h>
#include <stdint.h>

/*
 * Hardware mapping:
 *
 * Potentiometer:
 * one side       -> K64F 3.3V
 * other side     -> K64F GND
 * middle / wiper -> K64F ADC0_DP0
 *
 * Photoresistor:
 * 3.3V -> photoresistor -> ADC0_DP1
 * ADC0_DP1 -> resistor -> GND
 *
 * Pull-up switches:
 * SW0 -> PTB2  -> switch -> GND
 * SW1 -> PTB3  -> switch -> GND
 * SW2 -> PTB10 -> switch -> GND
 * SW3 -> PTB11 -> switch -> GND
 *
 * Mode button:
 * PTA1 -> button -> GND
 * Pull-up logic:
 * not pressed = HIGH
 * pressed     = LOW
 *
 * SPI:
 * K64F MOSI/SOUT -> Arduino D11 / PB3 / MOSI
 * K64F SCK       -> Arduino D13 / PB5 / SCK
 * K64F GND       -> Arduino GND
 */

#define SW0_MASK  (1UL << 2)    /* PTB2 */
#define SW1_MASK  (1UL << 3)    /* PTB3 */
#define SW2_MASK  (1UL << 10)   /* PTB10 */
#define SW3_MASK  (1UL << 11)   /* PTB11 */

#define MODE_BUTTON_MASK  (1UL << 1)   /* PTA1 */

/*
 * Direct NVIC register setup for PORTA interrupt.
 *
 * K64F PORTA IRQ number = 59.
 * 59 is in NVIC ISER1 / ICPR1, bit 59 - 32 = 27.
 */
#define PORTA_IRQ_NUM   59U
#define PORTA_IRQ_BIT   (1UL << (PORTA_IRQ_NUM - 32U))

#define NVIC_ISER1_REG  (*(volatile uint32_t *)0xE000E104)
#define NVIC_ICPR1_REG  (*(volatile uint32_t *)0xE000E284)

/*
 * RAM vector table setup.
 *
 * Cortex-M vector table index:
 * index = 16 + IRQ number
 * PORTA index = 16 + 59 = 75
 */
#define PORTA_VECTOR_INDEX  (16U + PORTA_IRQ_NUM)
#define SCB_VTOR_REG        (*(volatile uint32_t *)0xE000ED08)
#define VECTOR_TABLE_SIZE   256U

/*
 * Photoresistor normalization.
 *
 * Based on measured data:
 * normal/dark range often around 0-500
 * phone flashlight range around 8000-12600
 *
 * AUTO mode is inverted:
 * raw <= PHOTO_DARK_RAW   -> 100% brightness
 * raw >= PHOTO_BRIGHT_RAW -> 0% brightness
 */
#define PHOTO_DARK_RAW    500U
#define PHOTO_BRIGHT_RAW  10000U

/*
 * Smaller delay makes LED and button response faster.
 */
#define MAIN_LOOP_DELAY_COUNT   20000U

/*
 * Button lockout prevents one mechanical press from toggling multiple times.
 * The ISR toggles immediately, then ignores bounce edges for a few main-loop cycles.
 */
#define BUTTON_LOCKOUT_LOOPS    5U

/*
 * Print debug every N loops to avoid slowing down the system too much.
 */
#define DEBUG_PRINT_INTERVAL    10U

typedef enum {
    MODE_MANUAL = 0,
    MODE_AUTO = 1
} SystemMode;

unsigned char write[128];

/*
 * These variables are shared between ISR and main loop.
 * They must be volatile.
 */
volatile SystemMode currentMode = MODE_MANUAL;
volatile uint8_t reset_lock_active = 0;
volatile uint8_t modeButtonEventFlag = 0;
volatile uint32_t modeButtonLockout = 0;

/*
 * RAM vector table.
 * 256 entries x 4 bytes = 1024 bytes.
 * Align to 1024 bytes for VTOR.
 */
static uint32_t ramVectorTable[VECTOR_TABLE_SIZE] __attribute__((aligned(1024)));

/*
 * Forward declaration because Install_PORTA_Handler_In_RAM_Vector_Table()
 * uses PORTA_IRQHandler before the ISR definition.
 */
void PORTA_IRQHandler(void);

static void Install_PORTA_Handler_In_RAM_Vector_Table(void)
{
    uint32_t i;
    uint32_t *currentVectorTable;

    /*
     * Disable global interrupts while changing vector table.
     */
    __asm volatile ("cpsid i");

    /*
     * Copy current vector table to RAM.
     * SCB_VTOR_REG stores the current vector table base address.
     */
    currentVectorTable = (uint32_t *)(SCB_VTOR_REG & 0xFFFFFF80UL);

    for (i = 0; i < VECTOR_TABLE_SIZE; i++) {
        ramVectorTable[i] = currentVectorTable[i];
    }

    /*
     * Replace PORTA vector entry with our ISR.
     * PORTA IRQ = 59.
     * Cortex-M vector index = 16 + 59 = 75.
     */
    ramVectorTable[PORTA_VECTOR_INDEX] = (uint32_t)PORTA_IRQHandler;

    /*
     * Redirect vector table to RAM.
     */
    SCB_VTOR_REG = (uint32_t)ramVectorTable;

    /*
     * Synchronization barriers.
     */
    __asm volatile ("dsb");
    __asm volatile ("isb");

    /*
     * Re-enable global interrupts.
     */
    __asm volatile ("cpsie i");
}

static void ADC0_Init(void)
{
    SIM_SCGC6 |= SIM_SCGC6_ADC0_MASK;

    ADC0_CFG1 = 0x0C;   /* 16-bit ADC, bus clock */
    ADC0_SC2  = 0x00;   /* Software trigger, default reference */
    ADC0_SC3  = 0x00;   /* No continuous conversion, no averaging */
    ADC0_SC1A = 0x1F;   /* Disable ADC during initialization */
}

static uint16_t ADC0_Read_Channel(uint8_t channel)
{
    /*
     * ADC0_DP0 = channel 0x00
     * ADC0_DP1 = channel 0x01
     */
    ADC0_SC1A = channel;

    while (ADC0_SC2 & ADC_SC2_ADACT_MASK) {
    }

    while (!(ADC0_SC1A & ADC_SC1_COCO_MASK)) {
    }

    return (uint16_t)ADC0_RA;
}

static uint16_t ADC0_Read_Potentiometer(void)
{
    return ADC0_Read_Channel(0x00);   /* ADC0_DP0 */
}

static uint16_t ADC0_Read_Photoresistor(void)
{
    return ADC0_Read_Channel(0x01);   /* ADC0_DP1 */
}

static void Switches_Init(void)
{
    SIM_SCGC5 |= SIM_SCGC5_PORTB_MASK;

    /*
     * 0x103:
     * MUX = 001 -> GPIO
     * PE = 1    -> pull resistor enabled
     * PS = 1    -> pull-up
     */
    PORTB_PCR2  = 0x00000103;   /* SW0 = PTB2 */
    PORTB_PCR3  = 0x00000103;   /* SW1 = PTB3 */
    PORTB_PCR10 = 0x00000103;   /* SW2 = PTB10 */
    PORTB_PCR11 = 0x00000103;   /* SW3 = PTB11 */

    GPIOB_PDDR &= ~(SW0_MASK | SW1_MASK | SW2_MASK | SW3_MASK);
}

static uint8_t Read_Switch_Bits(void)
{
    uint8_t sw = 0;

    /*
     * Pull-up logic:
     * pin reads 0 -> switch ON
     * pin reads 1 -> switch OFF
     */
    if (!(GPIOB_PDIR & SW0_MASK)) {
        sw |= 0x01;
    }

    if (!(GPIOB_PDIR & SW1_MASK)) {
        sw |= 0x02;
    }

    if (!(GPIOB_PDIR & SW2_MASK)) {
        sw |= 0x04;
    }

    if (!(GPIOB_PDIR & SW3_MASK)) {
        sw |= 0x08;
    }

    return sw;
}

static void Mode_Button_Init(void)
{
    /*
     * Enable Port A clock gate.
     */
    SIM_SCGC5 |= SIM_SCGC5_PORTA_MASK;

    /*
     * PTA1 as GPIO input with pull-up and falling-edge interrupt.
     *
     * 0xA0103:
     * IRQC bits [19:16] = 1010 -> interrupt on falling edge
     * MUX bits  [10:8]  = 001  -> GPIO
     * PE bit = 1 -> pull resistor enabled
     * PS bit = 1 -> pull-up
     */
    PORTA_PCR1 = 0x000A0103;

    /*
     * PTA1 as input.
     */
    GPIOA_PDDR &= ~MODE_BUTTON_MASK;

    /*
     * Clear previous PORTA interrupt flag.
     */
    PORTA_ISFR = MODE_BUTTON_MASK;

    /*
     * Clear pending PORTA interrupt in NVIC.
     */
    NVIC_ICPR1_REG = PORTA_IRQ_BIT;

    /*
     * Enable PORTA interrupt in NVIC.
     */
    NVIC_ISER1_REG = PORTA_IRQ_BIT;
}

void PORTA_IRQHandler(void)
{
    if (PORTA_ISFR & MODE_BUTTON_MASK) {
        /*
         * Clear PORTA pin interrupt flag first.
         * Writing 1 clears the flag.
         */
        PORTA_ISFR = MODE_BUTTON_MASK;

        /*
         * Immediate mode toggle.
         * Do not call printf, ADC, SPI, or delay inside ISR.
         */
        if (modeButtonLockout == 0U) {
            if (currentMode == MODE_MANUAL) {
                currentMode = MODE_AUTO;
            } else {
                currentMode = MODE_MANUAL;
            }

            /*
             * Reset manual reset-lock when changing mode.
             */
            reset_lock_active = 0;

            /*
             * Tell main loop to print the mode-change message.
             */
            modeButtonEventFlag = 1;

            /*
             * Ignore bounce edges for a few main-loop cycles.
             */
            modeButtonLockout = BUTTON_LOCKOUT_LOOPS;
        }
    }

    /*
     * Clear pending PORTA interrupt in NVIC.
     */
    NVIC_ICPR1_REG = PORTA_IRQ_BIT;
}

static uint32_t Raw_To_Millivolts(uint16_t raw)
{
    return ((uint32_t)raw * 3300U) / 65535U;
}

static uint32_t Raw_To_Percent(uint16_t raw)
{
    uint32_t percent;

    percent = ((uint32_t)raw * 100U + 32767U) / 65535U;

    if (percent > 100U) {
        percent = 100U;
    }

    return percent;
}

static uint32_t Photo_Raw_To_Auto_Brightness(uint16_t photoRaw)
{
    uint32_t brightness;

    /*
     * Inverted mapping:
     * darker environment -> higher brightness
     * brighter environment -> lower brightness
     */
    if (photoRaw <= PHOTO_DARK_RAW) {
        return 100U;
    }

    if (photoRaw >= PHOTO_BRIGHT_RAW) {
        return 0U;
    }

    brightness = 100U -
        (((uint32_t)(photoRaw - PHOTO_DARK_RAW) * 100U +
          ((PHOTO_BRIGHT_RAW - PHOTO_DARK_RAW) / 2U))
         / (PHOTO_BRIGHT_RAW - PHOTO_DARK_RAW));

    if (brightness > 100U) {
        brightness = 100U;
    }

    return brightness;
}

static uint32_t Apply_Brightness(uint32_t brightness, uint32_t colorLevel)
{
    return (brightness * colorLevel + 50U) / 100U;
}

static void delay_loop(uint32_t count)
{
    volatile uint32_t i;

    for (i = 0; i < count; i++) {
    }
}

int main(void)
{
    /*** Processor Expert internal initialization. DON'T REMOVE THIS CODE!!! ***/
    PE_low_level_init();
    /*** End of Processor Expert internal initialization.                    ***/

    uint16_t pot_raw;
    uint16_t photo_raw;

    uint32_t pot_mv;
    uint32_t photo_mv;

    uint32_t pot_percent;
    uint32_t autoBrightness;

    uint8_t sw_state;

    uint32_t manualBrightness = 100U;
    uint32_t redLevel = 100U;
    uint32_t greenLevel = 100U;
    uint32_t blueLevel = 100U;

    uint32_t outRed;
    uint32_t outGreen;
    uint32_t outBlue;

    uint8_t ret;
    int len;

    uint32_t debugCounter = 0U;
    SystemMode modeSnapshot;

    LDD_TDeviceData *SM1_DeviceData;
    SM1_DeviceData = SM1_Init(NULL);

    /*
     * Install PORTA handler into RAM vector table before enabling PORTA interrupt.
     */
    Install_PORTA_Handler_In_RAM_Vector_Table();

    ADC0_Init();
    Switches_Init();
    Mode_Button_Init();

    printf("K64F integrated AUTO/MANUAL RGB control started.\n");
    printf("MANUAL: SW0=brightness, SW1=red, SW2=green, SW3=blue, all ON=reset.\n");
    printf("AUTO: photoresistor controls white brightness.\n");
    printf("Mode button: PTA1 falling-edge interrupt, immediate ISR toggle.\n");

    for (;;) {
        pot_raw = ADC0_Read_Potentiometer();
        photo_raw = ADC0_Read_Photoresistor();

        pot_mv = Raw_To_Millivolts(pot_raw);
        photo_mv = Raw_To_Millivolts(photo_raw);

        pot_percent = Raw_To_Percent(pot_raw);
        autoBrightness = Photo_Raw_To_Auto_Brightness(photo_raw);

        /*
         * Print mode-change message outside ISR.
         */
        if (modeButtonEventFlag) {
            modeButtonEventFlag = 0;

            printf("MODE TOGGLED: %s\n",
                   (currentMode == MODE_MANUAL) ? "MANUAL" : "AUTO");

            /*
             * Clear possible bounce-created pending flags.
             */
            PORTA_ISFR = MODE_BUTTON_MASK;
            NVIC_ICPR1_REG = PORTA_IRQ_BIT;
        }

        /*
         * Decrement button lockout in main loop.
         */
        if (modeButtonLockout > 0U) {
            modeButtonLockout--;
        }

        sw_state = Read_Switch_Bits();

        /*
         * Snapshot current mode for this loop iteration.
         */
        modeSnapshot = currentMode;

        if (modeSnapshot == MODE_MANUAL) {
            /*
             * Manual mode switch behavior.
             */
            if (sw_state == 0x0F) {
                /*
                 * All switches ON:
                 * reset everything to full white and full brightness.
                 * Then enter lockout until all switches are OFF.
                 */
                manualBrightness = 100U;
                redLevel = 100U;
                greenLevel = 100U;
                blueLevel = 100U;

                reset_lock_active = 1;
            } else if (reset_lock_active) {
                /*
                 * After reset, ignore all controls until all switches are OFF.
                 */
                if (sw_state == 0x00) {
                    reset_lock_active = 0;
                }
            } else {
                /*
                 * Normal manual control.
                 * Invalid switch combinations are ignored.
                 */
                if (sw_state == 0x01) {
                    manualBrightness = pot_percent;
                } else if (sw_state == 0x02) {
                    redLevel = pot_percent;
                } else if (sw_state == 0x04) {
                    greenLevel = pot_percent;
                } else if (sw_state == 0x08) {
                    blueLevel = pot_percent;
                } else {
                    /*
                     * sw_state == 0x00 or invalid combination:
                     * do nothing.
                     */
                }
            }

            outRed = Apply_Brightness(manualBrightness, redLevel);
            outGreen = Apply_Brightness(manualBrightness, greenLevel);
            outBlue = Apply_Brightness(manualBrightness, blueLevel);
        } else {
            /*
             * Auto mode:
             * photoresistor controls brightness.
             * RGB fixed to white.
             */
            outRed = autoBrightness;
            outGreen = autoBrightness;
            outBlue = autoBrightness;
        }

        /*
         * Debug output is intentionally throttled.
         * Printing every loop slows down button response.
         */
        debugCounter++;

        if (debugCounter >= DEBUG_PRINT_INTERVAL) {
            debugCounter = 0U;

            printf("MODE=%s, "
                   "POT_RAW=%5u, POT_MV=%4lu, POT_PERCENT=%3lu%%, "
                   "PHOTO_RAW=%5u, PHOTO_MV=%4lu, AUTO_BRI=%3lu%%, "
                   "SW=0x%02X, LOCK=%u, BTN_LOCK=%lu, "
                   "MAN_BRI=%3lu%%, R_LEVEL=%3lu%%, G_LEVEL=%3lu%%, B_LEVEL=%3lu%%, "
                   "TX=%lu,%lu,%lu\n",
                   (modeSnapshot == MODE_MANUAL) ? "MANUAL" : "AUTO",
                   (unsigned int)pot_raw,
                   (unsigned long)pot_mv,
                   (unsigned long)pot_percent,
                   (unsigned int)photo_raw,
                   (unsigned long)photo_mv,
                   (unsigned long)autoBrightness,
                   (unsigned int)sw_state,
                   (unsigned int)reset_lock_active,
                   (unsigned long)modeButtonLockout,
                   (unsigned long)manualBrightness,
                   (unsigned long)redLevel,
                   (unsigned long)greenLevel,
                   (unsigned long)blueLevel,
                   (unsigned long)outRed,
                   (unsigned long)outGreen,
                   (unsigned long)outBlue);
        }

        /*
         * Send RGB output percentages to Arduino.
         * Format:
         * R,G,B\n
         */
        len = sprintf((char *)write,
                      "%lu,%lu,%lu\n",
                      (unsigned long)outRed,
                      (unsigned long)outGreen,
                      (unsigned long)outBlue);

        ret = SM1_SendBlock(SM1_DeviceData, write, len);

        if (ret != ERR_OK) {
            printf("SM1_SendBlock failed. Error code = %u\n", ret);
        }

        delay_loop(MAIN_LOOP_DELAY_COUNT);
    }

#ifdef PEX_RTOS_START
    PEX_RTOS_START();
#endif

    for (;;) {
    }
}
