#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>

byte     analogread_power_pin = 2;
byte     speed_input_pin      = A2;
byte     pwm_output_pin       = 1;
bool     fan_running          = false;
uint32_t fan_stop_time;

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

  pinMode(analogread_power_pin, INPUT);
  pinMode(speed_input_pin, INPUT);
  pinMode(button_pin, INPUT_PULLUP);
  GIMSK = 1<<PCIE;       // pin change interrupt
  PCMSK = 1<<button_pin; // pin change interrupt

  // external 64 mhz timer clock: only tested it
  // needs to be turned on again after sleep
  //PLLCSR = 1<<PCKE | 1<<PLLE; 
  //PLLCSR |= 1<<LSM;               // low speed mode: 32 mhz
  //while(!(PLLCSR & (1<<PLOCK)));  // wait for stable PLL clock

  // custom 25 kHz PWM at 1 MHz system clock:
  // datasheet chapters:
  // 12.2 Counter and Compare Units
  // 12.3.1 TCCR1 – Timer/Counter1 Control Register
  TCCR1 &= 0xF0;                    // turn off timer clock / reset all prescaler bits (some are set by default)
  //TCCR1 |= (1<<CS12) | (1<<CS11); // prescaler: 32
  //TCCR1 |= (1<<CS12) | (1<<CS10); // prescaler: 16
  //TCCR1 |= (1<<CS12);             // prescaler: 8
  TCCR1 |= (1<<CS10);               // no prescaler

  //12.3.1 TCCR1 – Timer/Counter1 Control Register
  // Table 12-1. Compare Mode Select in PWM Mode
  // 
  //TCCR1 |= (1<<COM1A1);             // OC1x cleared on compare match. Set when TCNT1 = $00. Inverted OC1x not connected.
  // high and low time inverted:
  //TCCR1 |= (1<<COM1A1) | (1<<COM1A0); // OC1x Set on compare match. Cleared when TCNT1= $00.  Inverted OC1x not connected.
  // TCCR1 is set in start_fan()

  // this is how far the counter goes before it starts over again (255 max):
  OCR1C = 40;

  // example PWM values:
  // frequency: timer clock source (1 MHz) / prescaler (1) / OCR1C (40): 25000 Hz
  // duty cycle:
  // OCR1A = 20; // (50% of OCR1C)
  //
  // Notice that in this example each step (1..40) means a difference of 2.5%: The resolution is not so fine. 

  // OCR1A is set dynamicly in set_fan_speed()
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
 
  delay(2); // Wait for Vref to settle
  
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

  // novelty vcc calculation using map instead of 1125300L / ADC, no sketch size penalty!
  // this way it's easy to adjust the measured reference 1100 voltage (1100) for more accuracy
  vcc_mv = map(1023,0,ADC,0,1100);
  //
  // I noticed the internal reference sinks when vcc rises, for example 950 mV at 5 V
  //
  // vcc much higher then the calibated reference voltage are exaggerated and could be roughly compensated like this:
  //
  // if (vcc_mv > 3800){
  //    // example: 3800 is known to be accurate, 5200 was measured as 5700
  //    vcc_mv = map(vcc_mv,3800,5700,3800,5200);
  // }
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

   reset_running_time();

   // PWM mode OC1A: inverted duty cycle
   TCCR1 |= (1<<COM1A1) | (1<<COM1A0); // OC1x Set on compare match. Cleared when TCNT1= $00.  Inverted OC1x not connected.

   // start up sequence: full power first second
   // especially at lower VCC high duty cycle pulsating seems to start up the motor more successfully

   //  90% duty cycle
   OCR1A = OCR1C * 0.9;

   delay(1000);
   fan_running = true;
   return true;
}

bool stop_fan(){
   // turn off PWM mode OC1A:
   TCCR1 &= ~(1<<COM1A1 | 1<<COM1A0);

   digitalWrite(pwm_output_pin, HIGH);
   fan_running = false;
   go_to_sleep_forever();
}

void set_fan_speed(){
  // determine the lowest possible duty cycle / fan speed. The fan should still be able to spin.
  // allow a lower duty cycle when vcc is higher

  byte lower_limit_percentage = map(vcc_mv, 3000, 5000, 18, 8); // 18% at 3 V, 8% at 5 V
  lower_limit_percentage      = constrain(lower_limit_percentage, 8, 18); // don't exceed boundries

  pinMode(analogread_power_pin, OUTPUT);
  digitalWrite(analogread_power_pin, HIGH);

  // after potentiometer was connected to pin intead of vcc, the voltage reading didn't reach the upper limit anymore (breadboard might have made things worse)
  // change 1020 into 1023. Max speed is still possible.
  uint16_t analog_read_fanspeed = analogRead(speed_input_pin);
  if (analog_read_fanspeed >= 1020){
     analog_read_fanspeed = 1023;
  }

  pinMode(analogread_power_pin, INPUT); // high impedance-state

  // PWM mode OC1A: set duty cycle (OCR1A / OCR1C * 100)
  OCR1A = map(analog_read_fanspeed, 0, 1023, OCR1C * lower_limit_percentage / 100, OCR1C);
}

void reset_running_time(){
   if (vcc_mv > 4500){
      // run longer when probably on usb power
      fan_stop_time = millis() + 1000 * 600UL; 
   } else {
      fan_stop_time = millis() + 1000 * 300UL;
   }  
}

void loop() {
  if (button_press_temp){
      button_press_temp = 0;
      delay(40); // primitive debounce

      reset_running_time();
  }
  
  if (!fan_running){
     if (!start_fan()){
        // couldn't start
        go_to_sleep_forever();
        return;
     }  
  }

  if (millis() >= fan_stop_time){
     stop_fan();
     return;
  }

  check_vcc();
  if (vcc_mv <= 2900){
     stop_fan();
     return;
  }

  set_fan_speed();

  delay(200);
}
