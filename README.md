# mqtt_usb_switch
ESP32 firmware for interfacing with a Plugable 3.0 USB Switch to add MQTT Functionality

Device reports which output is currently being used via MQTT, to allow for home assistant automations to trigger based on which computer my USB devices are connected to (currently, my monitor changes to the proper inputs when the switch changes state, I'll be adding the monitor control hardware/firmware in a separate repo after further development.)

## Configuration
The `Menuconfig` options `Output A Name` and `Output B Name` name are the payloads that are sent to the `<MQTT_Topic>/state` topic on changing to output A/B, and are also the payloads received on the `<MQTT_Topic>/command` topic when a new state is desired.

The firmware was designed to first and foremost work with home assistant, and the defaults accomodate this.  Assuming you configure your `USB Switch MQTT Topic` as `home-assistant/usb_switch/usb_switch1`, states will be published by the device to `home-assistant/usb_switch/usb_switch1/state` by the device, and commands sent by home assistant will be published to the `home-assistant/usb_switch/usb_switch1/command` topic.

`MQTT Online Payload` will be published to the `available topic` (which, from defaults would be `home-assistant/usb_switch_usb_switch1/available`) will be published when the device connects, and `MQTT Offline Payload` will be published as a LWT when the device goes offline.  The defaults are designed to work with home assistant.

## Hardware

The IC we are connecting to uses 5v logic, signals need to be shifted  3v3 <-> 5v when connecting to the ESP32.  There is no 3v3 rail on the switch by default, so a 3v3 variant of the AMS1117 was added to compliment the 1v8 AMS1117 already on the board.

By default, `GPIO34` should be hooked to the output pin driving the LED for `Output A`, and `GPIO35` should be hooked to the output pin driving the LED for `Output B`. `GPIO15` is connected to the button input pin.

I just have a bare ESP-WROOM-32D inside my switch, so an RC delay circuit was added to the EN pin to ensure proper booting.

![board](images/board.png)

## ToDo

This is all very quick and dirty, more proof of concept than anything.  I at minimum want to come back and add support for MQTT key authentication, and add wifi provisioning and device reset code (at the moment I need to tear this thing down if I ever switch WiFi networks)