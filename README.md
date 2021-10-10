# SofronioEspressoRatioScale
Yet another ratio espresso scale<br />
Chinese Manual: https://github.com/Sofronio/SofronioEspressoRatioScale/blob/main/Manual.md<br />
PourOver Ratio Scale: https://github.com/Sofronio/SofronioPourOverRatioScale<br />
## Version:
### v1.5
* ADD Long press "咖啡录入" to rotate display.
* FIX During minus porta filter weight mode, timer doesn't function.
### v1.4
* FIX During mode chaning, timer doesn't clear to zero.
### v1.3
* FIX In espresso mode, timer will auto start.
### v1.2
* FIX Sample can't be changed.
* ADD During power up, press and hold "+" to set portafilter weight.
* ADD In pure scale mode, press "+" to show weight without portafilter.
* ADD Change vRef to match your MCU.
### v1.1
* ADD Battery icon.
### v1.0
* ADD During power up, press and hold TARE to calibrate.
* ADD During power up, press and hold INPUT to set sample.
* ADD During power up, press and hold "-" to show info.
## Features:
* Pure scale 
* Espresso scale
* Setting
### Pure Scale
* "咖啡录入" Set coffee powder, and switch to espresso mode.
* "+" Switch between raw weight and weigh minus portafilter.
* "-" Switch between 0.1g / 0.01g
* "清零" Scale tare.
### Espresso Scale
* Auto-tare negative weight.
* Auto-tare cup weight and start the timer.
* Show time stamp of the first coffee drop.
* Ratio display.
* Auto stop the timer when extraction compeleted.

* "咖啡录入" Click to start/stop/reset timer, long press to switch to pure scale.
* "+" Plus 0.1g coffee powder weight. Long press will increase faster.
* "-" Minus 0.1g coffee powder weight. Long press will decrease faster.
* "清零" Tare and stop/reset timer.
### Setting
During power on, press and hold the following buttons will...
* "咖啡录入" Set scale sample number. 1 for quick respond, and 128 for stability.
* "+" Set portafilter weight.
* "-" Show version info.
* "清零" Scale recalibration, 100g weight is needed.

# Library needed:
AceButton https://github.com/bxparks/AceButton <br />
Stopwatch_RT https://github.com/RobTillaart/Stopwatch_RT <br />
HX711_ADC https://github.com/olkal/HX711_ADC <br />
u8g2 https://github.com/olikraus/u8g2 <br />

# How to upload HEX file
Use OpenJumper™ Serial Assistant, link as below.<br />
https://www.cnblogs.com/wind-under-the-wing/p/14686625.html <br />
