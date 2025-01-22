# esp32-hci-proxy-target 
Experimental project aiming at creating virtual BT controllers using ESP32 microcontrollers. This package builds the firmware for ESP32 target.
It works together with the following host projects:

http://github.com/BogdanDIA/esp32-hci-proxy-addon.git

http://github.com/BogdanDIA/serial-udp.git

## Install firmware on the ESP32 target
The ESP32 board we tested with is: https://www.olimex.com/Products/IoT/ESP32/ESP32-DevKit-LiPo/open-source-hardware 

### Use ready built images
You will need `esptool` for flashing the images and this instructions are for Linux OS. It creates a virtual environment for installing `esptool` so that it will not taint your local system. First plug in your ESP32 USB cable to your computer.
```
python -m venv myesptool
source myesptool/bin/activate
pip install esptool
git clone https://github.com/BogdanDIA/esp32-hci-proxy-target.git
cd esp32-hci-proxy-target/hci_ip
```
Next command assumes the serial port is /dev/ttyUSB0
```
./flash_images.sh
```
Output shoudl be similar with the following:
```
esptool.py v4.7.0
Serial port /dev/ttyUSB0
Connecting.....
Chip is ESP32-D0WD-V3 (revision v3.1)
Features: WiFi, BT, Dual Core, 240MHz, VRef calibration in efuse, Coding Scheme None
...
```
### Build and flash images with ESP-IDF
If the esp-idf is installed on your system, then you could build and flashs the images directly:
```
git clone https://github.com/BogdanDIA/esp32-hci-proxy-target.git
cd esp32-hci-proxy-target/hci_ip
idf.py build flash monitor
```

## Connect the ESP32 to your AP
The ESP32 is configured with 115200 8N1 parameters.

If your ESP32 was set-up previously to connect to your AP then it will connect directly on the first boot. That is because the NVS (`Non Volatile Storage`) still keeps the WiFi credentials.

If NVS is erased, user will be prompted on the console to enter the credentials at the first boot, after testing the console read/write. In the example below it is shown a missing first character typed by the user but not received by ESP32 (a bug in ESP-IDF impl) followed by the request to fill in `ssid password`:
<pre>
E (669) HCI-IP_connect: WiFi credentials in NVS do not exist
I (679) HCI-IP_connect: Test console[0]: type <b>abcdefg</b> in terminal
Read from <b>console[0]:bcdefg</b> not OK, will retry
I (9019) HCI-IP_connect: Test console[1]: type <b>abcdefg</b> in terminal
Read from <b>console[1]:abcdefg</b> OK, will continue
I (11839) HCI-IP_connect: Please input ssid password:
</pre>

If NVS is not previously erased, two scenarions are possible.

To change SSID and password while already connected to AP, type blindly more than 10 'n' charachers like `nnnnnnnnnnnnnn` string folowed by Enter (just keep 'n' key pressed). It will trigger the change of credentials immediately.

To change the SSID and password while connection to AP is failing, type blindly more than 10 'f' charachers like `ffffffffffffffff` string folowed by Enter (just keep 'f' key pressed). It will wait until the AP connection with the current credentials fails and then will trigger credentials change as above.

For debugging purposes, the reason of last reboot is shown on the console upon processor boot. Following is a power supply brown-out situation:
<pre>
I (509) main_task: Calling app_main()
<b>E (509) CONTROLLER_HCI-IP: ESP_RST_BROWNOUT</b>
I (539) HCI-IP_connect: Start HCI-IP_connect.
</pre>

If AP connection is successful, the IP received from AP can be seen in the console. It will be needed later on when host is to be set-up:
```
I (4349) HCI-IP_common: - IPv4 address: 192.168.22.142
```

If ESP32 connects to the AP and receives the IP, all is set and it will wait for a conenction from the host.

