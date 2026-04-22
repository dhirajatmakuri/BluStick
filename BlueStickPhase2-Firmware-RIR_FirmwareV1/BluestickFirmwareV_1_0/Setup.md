# Bluestick BLE/WiFi Sniffer Run and Test set-up

Prerequisites
- ESP-IDF installed and configured (set IDF_PATH and added tools to PATH). See https://docs.espressif.com/projects/esp-idf/en/latest/.
- Toolchain for your ESP32 installed.

Build and flash
Open an ESP-IDF terminal in this project's root (the folder containing `CMakeLists.txt`) and run:

```powershell
# from project folder (BluestickFirmwareV_1_0/bluestick_ble_wifi_sniffer)
idf.py set-target esp32
or idf.py  set-target esp32s3 # if using the older model for the bluesticks
idf.py build
idf.py -p <PORT> flash monitor
```

Replace `<PORT>` with your serial port (e.g. COM3) or use idf.py flash monitor (without the com port) .

If using plain CMake/Make workflows, consult your ESP-IDF version docs.

Notes
- The original project had a more complex sniffer application; this replaces `main/bluestick_ble_wifi_sniffer.c` with a minimal example to verify the board and toolchain.
- To restore original behavior, re-add the previous main source code.


NOTE: to set a new target delete the old one using :
Remove-Item Env:IDF_TARGET

and use this to erase all the stuff on the flash: 
idf.py -p COM5 erase-flash

then use the normal set target and build methods