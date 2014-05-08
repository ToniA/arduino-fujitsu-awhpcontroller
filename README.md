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

Purpose of the relays
---------------------

Relay 1
* Controls the heatpump defrost cheating
   * Connects a 68kOhm resistor in parallel with the outdoor unit's pipe temp sensor, to prevent the unit from defrosting
   * One of the 1-wire sensors measures the pipe temp, and the relay is controlled by the difference between the outdoor air and pipe temp temperatures
* See the function 'awhpDefrostSignal'

Relay 2
* Controls the sewer pipe heating, and also the compressor heating cable
* By default the heating is on practically from October to March, now it's only running when really needed
* This relay does not control the heating directly, but just shows two different (fixed) temperature values to Ouman EH-203, the real relay and program is on the Ouman side
* See the function 'sewerHeatingCableSignal'

Relay 3
* Controls the 6 kW backup heater
   * Well, actually it's the Ouman EH-203 which controls the heater, based on boiler temperature measurements
   * The purpose of this relay is to 'cheat' the temperature measurement so that the backup heater will not turn on when the heatpump is not running
   * So this relay connects a resistor in parallel with the Ouman's NTC resistor, so that Ouman will see higher temperatures
* See the function 'awhpState', for the handling of the 'BACKUP_HEATER'

Relay 4
* Controls the heatpump, by connecting either 0 Ohm or 2.2 kOhm in series with the indoor unit's inlet air sensor
* The heatpump is set to 27 degrees, so with 0 Ohm resistor it will try to produce cooler than 30 degrees C, and with 2.2 kOhm resistor it will try to produce about 37 degrees C
* The purpose of this relay is to let the heatpump idle when it's running on very low power
   * When the boiler gets warm enough, let the heatpump idle until the boiler has cooled down a bit
   * Experience has shown that the heatpump runs on very poor COP on low power, so the whole point is to get better COP out of the heatpump
* See the function 'awhpState', for the handling of the 'AWHP_RUN'

Photo of the Controller
-----------------------
![Photo](https://raw.githubusercontent.com/ToniA/arduino-fujitsu-awhpcontroller/master/controller.jpg)
