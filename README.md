# DuetWiFiSocketServer

Firmware for ESP8266 Wi-Fi modules on Duet boards.

## Build

Building the project generates `DuetWiFiServer.bin` used for [`M997 S1`](https://duet3d.dozuki.com/Wiki/M997).

### Terminal (Linux/macOS)

1. Install the prequisites for your OS. [Linux](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/linux-setup.html#install-prerequisites) [macOS](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/macos-setup.html#install-prerequisites)
2. [Get](https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/get-started/index) ESP8266 RTOS SDK. Checkout branch `release/v3.4`.

3. Navigate to the ESP8266 RTOS SDK directory and execute the install script.

```console
user@pc:/path/to/ESP8266_RTOS_SDK$ ./install.sh
```

4. Export environment variables for the current terminal session.


```console
user@pc:/path/to/ESP8266_RTOS_SDK$ . ./export.sh
```

5. Navigate to the DuetWiFiSocketServer directory and execute the build command. Once the build finishes, `DuetWiFiServer.bin`  will be in the `build` directory.


```console
user@pc:/path/to/DuetWiFiSocketServer$ idf.py build
```

or 

```console
user@pc:/path/to/DuetWiFiSocketServer$ make
```

### Terminal (Windows)

1. Get the [pre-packaged environment (based on MSYS) and toolchain](https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/get-started/windows-setup.html). Extract both to a directory on your system (recommend `C:\` for the MSYS environment, and `C:\msys32\opt` for the toolchain). Open a [MSYS MINGW32 window](https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/get-started/windows-setup.html#check-it-out), as this will be used for the rest of the instructions.


2. [Get](https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/get-started/index.html#get-esp8266-rtos-sdk) ESP8266 RTOS SDK. Checkout branch `release/v3.4`. Once done, [install the Python prerequisites](https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/get-started/index.html#install-the-required-python-packages).

3. Set `IDF_PATH` environment variable and add the toolchain binary directory to `PATH` for the current terminal session. *Note: Replace `/opt/xtensa-lx106-elf/bin` with correct path if toolchain was not extracted to `C:\msys32\opt`.*

```console
user@pc MINGW32 ~
$ export IDF_PATH=/path/to/ESP8266_RTOS_SDK

user@pc MINGW32 ~
$ export PATH=/opt/xtensa-lx106-elf/bin:$PATH
```

4. Navigate to the DuetWiFiSocketServer directory and execute the build command. Once the build finishes, `DuetWiFiServer.bin`  will be in the `build` directory.

```console
user@pc MINGW32 /path/to/DuetWiFiSocketServer
$ make
```


### Eclipse

Instructions for setting up building with Eclipse can be found [here](https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/get-started/eclipse-setup.html) for Linux/macOS and [here](https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/get-started/eclipse-setup-windows.html#eclipse-windows-setup) for Windows.

Import this project in the `Import New Project` step.

## Links

[Forum](https://forum.duet3d.com/) [Wiki](https://duet3d.dozuki.com/)
