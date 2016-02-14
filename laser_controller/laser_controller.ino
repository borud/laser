/**
 * Controller for laser.  This program is for guiding experiments with
 * a laser using remote serial control.
 *
 * @author borud
 */

#include <string.h>
#include <CustomStepper.h>

#define VERSION_STRING "0.1.0"

#define SERIAL_SPEED 115200

// Emergency shutoff button
#define EMERGENCY_SHUTOFF 4

// Laser control pins
#define PIN_LASER_PWM    A3
#define PIN_LASER_ARM     7

// Stepper pins
#define PIN_IN1       8
#define PIN_IN2       9
#define PIN_IN3      10
#define PIN_IN4      11

// Make sure this matches the motor used
#define STEPPER_SPR          4075.7728395
#define STEPPER_RPM            15
#define MAX_FOCUS_ROTATION   1440

// Serial buffer used for command input
#define SERIAL_BUFFER_SIZE 80

static char buffer[SERIAL_BUFFER_SIZE];
static byte buffer_offset = 0;
static boolean overflow_mode = false;

CustomStepper stepper(PIN_IN1, PIN_IN2, PIN_IN3, PIN_IN4,
                      (byte[]){8, B1000, B1100, B0100, B0110, B0010, B0011, B0001, B1001},
                      STEPPER_SPR,
                      STEPPER_RPM,
                      CW);

boolean stepper_run = false;
long stepper_pos = 0;
byte laser_duty_cycle = 0;
boolean laser_armed = false;

/**
 * Set up the application.
 */
void setup() {
    Serial.begin(SERIAL_SPEED);

    // Set up ports for laser
    pinMode(PIN_LASER_ARM, OUTPUT);
    digitalWrite(PIN_LASER_ARM, LOW);
    analogWrite(PIN_LASER_PWM, 0);    
    
    // Initialize stepper
    stepper.setRPM(STEPPER_RPM);
    stepper.setSPR(STEPPER_SPR);

    memset(buffer, 0, SERIAL_BUFFER_SIZE);
    Serial.print(F("100 Laser Controller,  v"));
    Serial.print(VERSION_STRING);
    Serial.println(F(", <borud@borud.org>"));
}

void step_focus(int n) {
    if (abs(n) > MAX_FOCUS_ROTATION) {
        Serial.print(F("521 MAXIMUM FOCUS ROTATION IS "));
        Serial.println(MAX_FOCUS_ROTATION);
        return;
    }
    
    if (n > 0) {
        stepper.setDirection(CCW);
        stepper.rotateDegrees(n);
    } else {
        stepper.setDirection(CW);
        stepper.rotateDegrees(-n);
    }

    Serial.print(F("204 FOCUS "));
    Serial.println(n);

    stepper_pos += n;
    stepper_run = true;
}

void emergency_stop() {
    // Turn off laser and turn PWM duty cycle down
    digitalWrite(PIN_LASER_ARM, LOW);
    analogWrite(PIN_LASER_PWM, 0);
    laser_duty_cycle = 0;
    laser_armed = false;

    Serial.println(F("520 LASER OFF (unarmed, duty_cycle = 0"));
    
    // Stop any rotation
    if (! stepper.isDone()) {
        stepper.setDirection(STOP);
        stepper_run = false;
        Serial.println(F("521 STEPPER STOPPED, POSITION LOST"));
    }
}

void step_zero_pos() {
    Serial.print(F("209 FOCUS ZEROED, old pos = "));
    Serial.println(stepper_pos);
    stepper_pos = 0;
}

void laser_set_duty_cycle(byte n) {
    laser_duty_cycle = n;
    Serial.print(F("206 DUTY CYCLE "));
    Serial.println(laser_duty_cycle);
}

void laser_fire(long n) {
    if (! laser_armed) {
        Serial.println(F("530 LASER UNARMED"));
        return;
    }
    
    analogWrite(PIN_LASER_PWM, laser_duty_cycle);
    delay(n);
    analogWrite(PIN_LASER_PWM, 0);
    Serial.print(F("205 LASER FIRED "));
    Serial.print(n);
    Serial.print(F(" ms @ "));
    Serial.print(laser_duty_cycle);
    Serial.println(F("/255 duty cycle"));
}

void laser_arm() {
    digitalWrite(PIN_LASER_ARM, HIGH);
    laser_armed = true;
    Serial.println(F("207 LASER ARMED"));
}

void laser_unarm() {
    digitalWrite(PIN_LASER_ARM, LOW);
    laser_armed = false;
    Serial.println(F("208 LASER UNARMED"));
}

void parse_command(char* s) {
    static char message_buffer[50];

    switch (s[0]) {
        case 'e':
            emergency_stop();
            break;

        case 'a':
            laser_arm();
            break;
            
        case 'u':
            laser_unarm();
            break;

        case 'd':
            laser_set_duty_cycle(atoi(s+1));
            break;

        case 'f':
            laser_fire(atoi(s+1));
            break;
            
        case 'p':
            step_focus(atoi(s+1));
            break;

        case 'z':
            step_zero_pos();
            break;

        case 'h':
            print_help();
            break;

        default:
            Serial.println(F("501 UNKNOWN COMMAND"));
            break;
    }
}

void print_help() {
    Serial.println(
        F(
            "202 Commands:\n"
            "202   e           :  emergency stop\n"
            "202   d [0..254]  :  set laser duty cycle\n"
            "202   f [n]       :  fire laser for [n] milliseconds\n"
            "202   p [n]       :  focus by [n] degrees (primary cog)\n"
            "202   z           :  zero focus coordinates\n"
            "203 OK"
        )
    );
}

void read_serial() {
    int loop_count = 0;
    
    while (Serial.available()) {
        buffer[buffer_offset] = Serial.read();

        // If we are in overflow mode just dump chars until we see a
        // NL or we have reached the loop count.  Reaching the max
        // loop count dumps us out and gives the rest of the main
        // loop() a chance to get something done.
        if (overflow_mode) {
            loop_count++;

            if ('\n' == buffer[buffer_offset]) {
                overflow_mode = false;
                return;
            }

            if (loop_count == SERIAL_BUFFER_SIZE) {
                Serial.println(F("510 OVERFLOW"));
                return;
            }
        }

        // If we encounter a newline we have an entire command so we
        // parse it and reset the buffer offset.
        if ('\n' == buffer[buffer_offset]) {
            buffer[buffer_offset] = 0;
            buffer_offset = 0;            
            parse_command(buffer);
            return;
        }

        // Advance buffer offset if the previous character was not a NL
        buffer_offset++;

        // If the buffer is full we just reset the buffer and dump the
        // contents.
        if (SERIAL_BUFFER_SIZE == buffer_offset) {
            buffer_offset = 0;
            overflow_mode = true;
            Serial.println(F("510 OVERFLOW"));
            return;
        }
    }
}


void loop() {
    read_serial();
    
    if (! stepper.isDone()) {
        stepper.run();

        if (stepper.isDone()) {
            stepper_run = false;
            Serial.print(F("201 FOCUS DONE, pos = "));
            Serial.println(stepper_pos);
        }
    }
}
