# heat controller

This is a project that have been in the planning stages for the last couple of years, as I wanted to "modernize" our floor heating system, from an electromechanical switch to something MySensorized.

The old thermostat consists of a electrically operated valve, and a sensor that contains a relay, the temperature is controlled by a small screw on top, and it is hard to hit the right values as just a notch to the wrong side it is either too cold, or too hot.

One of the things that had kept me back, was actually the building phase, as soon as I have to build a birdsnest of something (like using regular arduino, and add wires to connect the radio etc.) I tend to keep my self from doing it, it's too much hassle and I have too many projects going on :) 

I had an early prototype of the sensebender gateway laying in the parts bin, so I used that to build my example.. (probably way overkill, but it has the radio connector LDO regulator etc. in place for the radio). One of the added advantages of using the gateway (SAMD based) is that it has an RTC onboard, which makes timing a bit easier, for when to report temperatures etc (it reports on a temperature change of more than 0.5 degrees, or every half hour)

I have 4 DS1820 sensors connected, they are attached as follows:

1. floor heating loop
2. hot water inlet from heating supplier
3. "cold" water out to the heating supplier
4. hot water for showers etc.

The enclosed sketch implements a simple statemachine, with 5 states:
- Heating
- - Heats up the floor loop
- Cooling
- - lets it cool down, switching off the valve and circulation pump
- Check floor temperature
- - turns on circulation pump for 2 minutes and then checks temperature of the loop.
- fetching hot water
- - This is a convenience state, sometimes (specially in the summer where we have the heating turned off) it takes some time to get hot water into the house from the supplier. This state turns on the valve to get hot water into the building. Theory is that we can save a little on the normal water usage. 
- summer mode
- - Turning the system "off"


All of the above is now controlled via domoticz, measurement data is also stored in an influx database, so I can create nice graphs with grafana
