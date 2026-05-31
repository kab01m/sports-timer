# Sports timer

## Requirements

Custom made sports timer. Large timer screen with IR remote, three LED on top, one LED on each side.

* Button 1:
  * Top LED green for 15 minutes.
  * Top LED blink red for 1 minute.
  * Cycle 4x times.
  * Off.

* Button 2
  * Top LED green for 5 minutes.
  * Top LED blink yellow for 1 minute.
  * Top LED red till turned OFF.

* Button 3
  * Top LED green for 2 hours.
  * Top LED red till turned OFF.

* Button 4
  * Top LED green for 1 hours 40 minutes.
  * Top LED red till turned OFF.

* Button LEFT turn on/off left green light.
* Button RUGHT turn on/off right green light.
* Button RESET turn everything off.

## Hardware

Large LED sports clock with MCU HC89F0541 (MCU is stripped off), lights based on TC5020EJ.

Top lights are 3-color 9-LED arrays, driven by PWM. Full brighntess 50ms period with 20ms impulse in it. R3 red, R9 green.

Left/right lights are 3-color 2-LED array.

Arduino PWM: 3,5,6,9,10,11

## Arduino layout

* Main clock connections
  * A2/16  (9  pin of MCU) - Hardware buttons 1-5
  * A0/14  (11 pin of MCU) - termperature sensor
  * D2/2   (15 pin of MCU) - SDI (2  pin of TC drivers via 100 ohm)
  * D3/3   (14 pin of MCU) - CLK (3  pin of TC drivers)
  * D5/5   (13 pin of MCU) - LE  (4  pin of TC drivers)
  * D6/6   (16 pin of MCU) - OE  (21 pin of TC drivers), reversed, used in PWM to control brightness
  * D9/9   (12 pin of MCU) - IR sensor
* Top lights
  * D9/9   - Green PWM color
  * D10/10 - Red PWM color
