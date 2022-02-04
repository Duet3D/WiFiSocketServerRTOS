# DuetWiFiSocketServer

Firmware for ESP8266 Wi-Fi modules on Duet boards.

## Build

Building the project generates `DuetWiFiServer.bin` used for [`M997 S1`](https://duet3d.dozuki.com/Wiki/M997).

### Terminal (Linux/macOS)

1. Install the prequisites for your OS: [Linux](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/linux-setup.html#install-prerequisites) [macOS](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/macos-setup.html#install-prerequisites).
2. Download/clone the latest [ESP8266 RTOS SDK release](https://github.com/espressif/ESP8266_RTOS_SDK). If the release ZIP file was downloaded, extract to your desired directory.
3. Navigate to the ESP8266 RTOS SDK directory and execute the install script.

```console
user@pc:/path/to/ESP8266_RTOS_SDK$ ./install.sh
```

4. Export environment variables for current terminal session.


```console
user@pc:/path/to/ESP8266_RTOS_SDK$ . ./export.sh
```

5. Navigate to the DuetWiFiSocketServer directory and execute the build command. Once the build finishes, `DuetWiFiServer.bin`  will be in the `build` directory.


```console
user@pc:/path/to/DuetWiFiSocketServer$ idf.py build
```

## Links

[Forum](https://forum.duet3d.com/) [Wiki](https://duet3d.dozuki.com/)
