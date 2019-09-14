#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>

byte     speed_input_pin    = A2;
byte     pwm_output_pin     = 1;
bool     fan_running        = false;
uint16_t stop_motor_counter = 100;

#include <util/delay.h>    // Adds delay_ms and delay_us functions

uint16_t vcc_mv;

volatile bool button_press_temp;
bool button_pressed;
//bool long_press;
//bool quick_press;
byte button_pin = 3; // attiny85 pin 2

ISR (PCINT0_vect){
    if (!button_state()){
      button_press_temp = 1;
   }
}

// watchdog interrupt
ISR (WDT_vect){
   wdt_disable();  // disable watchdog
}

void setup(){  
  pinMode(pwm_output_pin, OUTPUT);
  digitalWrite(pwm_output_pin, HIGH); // PNP: HIGH: motor off

  delay(200);  // VCC is measured before each motor start
               // give VCC a moment to stabilize
  check_vcc(); // prenimilary reading, first reading seems to be lower than expected

  pinMode(speed_input_pin, INPUT);
  pinMode(button_pin, INPUT_PULLUP);
  GIMSK = 1<<PCIE;       // pin change interrupt
  PCMSK = 1<<button_pin; // pin change interrupt

  // standard pwm frequency of 500 hz causes audible whine, most noticable at lower speed
  // increase pwm frequency: 
  // https://www.electroschematics.com/14089/attiny85-pwm-primer-tutorial-using-arduino/
  // 1 mhz: 4 khz: still noisy
  // 8 mhz: 32 khz: silent, but needs much higher duty cycle
  //TCCR0B = TCCR0B & 0b11111001; // this actually doesn't work...
  // this works:
  TCCR0B = TCCR0B & 0b11111000 | 0b001;

  // https://www.re-innovation.co.uk/docs/fast-pwm-on-attiny85/
  // delay isn't accurate anymore after changing TCCROB, use _delay_ms() included in delay.h instead
}

void check_vcc() {
  // modified version of readVcc found on http://digistump.com/wiki/digispark/quickref

  // Read 1.1V reference against AVcc
  // set the reference to Vcc and the measurement to the internal 1.1V reference
  #if defined(__AVR_ATmega32U4__) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
    ADMUX = _BV(REFS0) | _BV(MUX4) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #elif defined (__AVR_ATtiny24__) || defined(__AVR_ATtiny44__) || defined(__AVR_ATtiny84__)
    ADMUX = _BV(MUX5) | _BV(MUX0);
  #elif defined (__AVR_ATtiny25__) || defined(__AVR_ATtiny45__) || defined(__AVR_ATtiny85__)
    ADMUX = _BV(MUX3) | _BV(MUX2);
  #else
    ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  #endif  
 
  //delay(2); // Wait for Vref to settle
  // delay() doesn't work with changed timers.. (tccr0b: 32 khz pwm), got wrong measurements (too low) under certain circumstances..
  _delay_ms(2);
  
  //sleep_1_sec();
  ADCSRA |= _BV(ADSC); // Start conversion
  while (bit_is_set(ADCSRA,ADSC)); // measuring

  // original code:
  // return 1125300L / ADC; // Calculate Vcc (in mV); 1125300 = 1.1*1023*1000
  // 
  // After the measurement, ADC contains the relative value of vref compared to vcc
  // - ADC contains vref as a fraction (0-1023) of vcc
  // - if vref and vcc are equal then ADC would be 1023
  // - internal reference voltage is known to be about 1.1 v
  //
  // Example values:
  // vcc  | int. ref | ADC
  // -----+----------+-----
  //  1.1 | 1.1      | 1023
  //  2.2 | 1.1      | 511
  //  3.3 | 1.1      | 341
  //  4.4 | 1.1      | 255

  // my alternative using map also works
  // no sketch size penalty!
  // this way it's easy to adjust the measured reference (1100) voltage for more accuracy
  //uint16_t vcc_mv = map(1023,0,ADC,0,1100);
  vcc_mv = map(1023,0,ADC,0,1072); // 1072 measured at 3.05 V
}

void go_to_sleep_forever(){
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  ADCSRA &= ~(1<<ADEN);

  power_all_disable();  // power off ADC, Timer 0 and 1, serial interface

  noInterrupts();       // timed sequence coming up
  
  sleep_enable();       // ready to sleep
  interrupts();         // interrupts are required now
  sleep_cpu();          // sleep

  // from the ATTinyCore docs:
  // When using the WDT as a reset source and NOT using a bootloader remember that after reset the WDT will be enabled with minimum timeout.
  // The very first thing your application must do upon restart is reset the WDT (wdt_reset()),  clear WDRF flag in MCUSR (MCUSR&=~(1<<WDRF))
  // and then turn off or configure the WDT for your desired settings. If using the Optiboot bootloader, this is already done for you by the bootloader.
  wdt_reset();
  MCUSR&=~(1<<WDRF);
  wdt_disable();
 
  sleep_disable();      // precaution
  power_all_enable();   // power everything back on
  ADCSRA |= (1<<ADEN);  // ADC on. ADC only seems to work correctly when this is placed after power_all_enable(), otherwise analogRead readings are incorrect
}

bool button_state() {
   return PINB & 1<<button_pin;
}

bool start_fan(){
   check_vcc();
   if (vcc_mv <= 3000){
      return false;
   }

   // the counter increases about 6.25 times per second when the fan runs
   // 5 minutes: 300 * 6.25 = 1875
   stop_motor_counter = 1875;
   
   if (vcc_mv > 4500){
      stop_motor_counter *= 2; // double the running time when probably on usb power
   }

   // start up sequence
   // full power first second

   // especially at lower VCC high duty cycle pulsating seems to start up the motor more successfully
   analogWrite(pwm_output_pin, 60); // PNP: inversed duty cycle 
   _delay_ms(1000);
   digitalWrite(pwm_output_pin, LOW);
   fan_running = true;
   return true;
}

bool stop_fan(){
   digitalWrite(pwm_output_pin, HIGH);
   fan_running = false;
   go_to_sleep_forever();
}

void set_fan_speed(){
  // determine the lowest possible duty cycle / fan speed. The fan should still be able to spin.
  // allow a lower duty cycle when vcc is higher
  // because of PNP setup, the duty cycle is inversed. Lower limits:
  // 5 V: 240
  // 3 V: 220
  
  byte lower_limit = map(vcc_mv, 3000, 5000, 220, 240);
  lower_limit = constrain(lower_limit, 220, 250); // don't exceed lower boundry of 250
  
  byte fan_speed = map(analogRead(speed_input_pin), 0, 1023, lower_limit, 0);
  analogWrite(pwm_output_pin, fan_speed);
}

void loop() {
  // like delay(), millis() also doesn't seem to work anymore, use a counter instead
  static uint16_t counter;

  if (button_press_temp){
      button_press_temp = 0;
      _delay_ms(40); // primitive debounce

      // reset the counter
      counter = 0;
  }
  
  if (!fan_running){
     if (start_fan()){
        counter = 0;
     } else {
        // couldn't start
        go_to_sleep_forever();
        return;
     }  
  }

  counter++; // the counter increases about 6.25 times per second when the fan runs

  if (counter > stop_motor_counter){
     stop_fan();
     return;
  }

  check_vcc();
  if (vcc_mv <= 3000){
     stop_fan();
     return;
  }

  set_fan_speed();

  _delay_ms(100);
}
