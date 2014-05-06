arduino-fujitsu-awhpcontroller
==============================

The purpose of this project is to add some extra controls for the DIY Fujitsu air-to-water heatpump:
* The COP (coefficient of performance) of the heatpump is really bad when running on low power. For the heatpump to idle state when the output power need is low
* Disable the 6 kW backup immersion heater when the heatpump is in idle state. Normally this heater turn on when the heatpump does not produce warm enough water
* Cheat the heatpump to not defrost every 35 minutes on cold weather, but only when it's necessary
* Control the heating cable on the defrost water drain pipe
* Automatically start the air-to-air heatpumps when the outdoor air gets cold enough, i.e. when the air-to-water heatpump is not powerful enough
* Provide temperature logging using xPL messages

The DIY heatpump project can be found here (sorry, in Finnish): http://lampopumput.info/foorumi/index.php?topic=10949.0

The Bill of Materials
---------------------
* Arduino Duemilanove
* Arduino Ethernet shield
* 4-channel relay board
* 4x DS18B20 1-wire temperature sensors
* 2x16 characters LCD display, based on the Hitachi HD44780 driver

Photo of the Controller
-----------------------
![Photo](https://raw.githubusercontent.com/ToniA/arduino-fujitsu-awhpcontroller/master/controller.jpg)
