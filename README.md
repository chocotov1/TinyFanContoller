# TinyFanController

ATtiny85 based fan controller I built into a small USB powered desk fan.

## Features of my project build
- added speed control per potentiometer
- added JST PH lipo battery connector to fan, crimped JST connector onto original USB cable
- voltage dependent lowest speed control (stalled motor prevention)
- burst start (stalled motor prevention)
- timer shuts off fan after a while
- push button resets timer / starts fan
- low voltage protection / start prevention

## Fan motor

The fan in my project is driven by a brushless external rotor motor. Furthermore, the original controlling circuit is enclosed and I decided to not brake it open.

Some characteristics of the motor:
- two wires
- only spins in one direction (nothing happens when polarity is reversed)
- spins faster when voltage is increased 

I've searched around the web and found a few discussions debating if it's ok to switch such a motor on the low side. Some say it wouldn't be optimal. In this project i'm switching the fan on the high side with a PNP transistor without problems. I didn't try low side switching.

## First attempt: Only BJT transistor, no MCU

To make the hack as simple as possible I first tried only using a BJT with a potentiometer. That works too, especially at full power. At lower speeds though the transistor just acts like a resistor and heats up. I didn't want that.

## MCU version: PWM whine

The first attempt with a PWM signal from the ATtiny produced an audible whine. This was using the regular 500 Hz PWM signal of the ATtiny / Arduino.

After I changed to a higher frequency PWM (32 KHz), the whine was gone. This came at the expense of not being able to use delay() and millis() anymore.

## Todo: Custom PWM signal

In my other project [DHT11Tiny](https://github.com/chocotov1/DHT11Tiny) I first came across configuring the ATTiny timers directly. I'm planning on configuring a custom PWM signal directly in a future version of the fan controller.

Some more hints can be found here:

https://www.electroschematics.com/14089/attiny85-pwm-primer-tutorial-using-arduino/

## Project build images

<img src="https://raw.githubusercontent.com/chocotov1/TinyFanController/master/media/TinyFanController_build_010.jpg" width=640>
<img src="https://raw.githubusercontent.com/chocotov1/TinyFanController/master/media/TinyFanController_build_020.jpg" width=640>
<img src="https://raw.githubusercontent.com/chocotov1/TinyFanController/master/media/TinyFanController_build_030.jpg" width=640>
<img src="https://raw.githubusercontent.com/chocotov1/TinyFanController/master/media/TinyFanController_build_040.jpg" width=640>
<img src="https://raw.githubusercontent.com/chocotov1/TinyFanController/master/media/TinyFanController_build_050.jpg" width=640>
<img src="https://raw.githubusercontent.com/chocotov1/TinyFanController/master/media/TinyFanController_build_060.jpg" width=640>
