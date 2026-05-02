#include <Arduino.h>

/*
  EE128 Lab 4 - Arduino Uno R3 PWM Generator

  Board:
    Arduino Uno R3

  Output:
    Arduino D3 = ATmega328P PD3 = PWM output

  Connection:
    Arduino D3 -> voltage divider -> K64F PTC10 / FTM3_C6
    Arduino GND ------------------> K64F GND

  Important:
    Do NOT connect Arduino D3 directly to K64F PTC10.
    Arduino Uno outputs 5V logic.
    K64F uses 3.3V logic.
*/

const int PWM_PIN = 3;              // Arduino D3 = PD3
const unsigned long DELAY_MS = 1000; // Lab manual asks delay > 500 ms

const int pwm_values[] = {
    64,   // about 25%
    127,  // about 50%
    191,  // about 75%
    252,  // about 99%
    191,
    127,
    64
};

const int num_values = sizeof(pwm_values) / sizeof(pwm_values[0]);

void setup()
{
    pinMode(PWM_PIN, OUTPUT);

    Serial.begin(9600);
    Serial.println("EE128 Lab 4 Arduino PWM Output Started");

    analogWrite(PWM_PIN, 127); // start around 50%
}

void loop()
{
    for (int i = 0; i < num_values; i++)
    {
        int pwm_value = pwm_values[i];

        analogWrite(PWM_PIN, pwm_value);

        int duty_percent = (pwm_value * 100L) / 255;

        if (duty_percent > 99)
        {
            duty_percent = 99;
        }

        Serial.print("PWM value: ");
        Serial.print(pwm_value);
        Serial.print(" | Duty: ");
        Serial.print(duty_percent);
        Serial.println("%");

        delay(DELAY_MS);
    }
}