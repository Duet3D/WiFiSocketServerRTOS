# WiFiSocketServerRTOS

Firmware for Espressif Wi-Fi modules on Duet boards. Based on [DuetWiFiSocketServer](https://github.com/Duet3D/DuetWiFiSocketServer), but ported to the newer ESP8266 RTOS SDK and ESP-IDF.

## Build

Building the project generates `DuetWiFiServer.bin` or `DuetWiFiModule_*.bin` used for [`M997 S1`](https://docs.duet3d.com/User_manual/Reference/Gcodes/M997). ESP8266, ESP32, ESP32-S3 and ESP32-C3 are supported.

### Makefile (recommended)

The Makefile-based build handles all targets and SDK setup automatically. Prerequisites:

1. Clone the SDKs as sibling directories:

```console
user@pc:/path/to$ git clone --branch dwss_support --recursive https://github.com/Duet3D/ESP8266_RTOS_SDK.git
user@pc:/path/to$ git clone --branch dwss_support --recursive https://github.com/Duet3D/esp-idf.git
```

2. Run the install scripts in each SDK to set up toolchains and Python environments:

```console
user@pc:/path/to/ESP8266_RTOS_SDK$ ./install.sh
user@pc:/path/to/esp-idf$ ./install.sh
```

3. Build:

```console
user@pc:/path/to/WiFiSocketServerRTOS$ make all
```

Available targets:

| Target | Output |
|--------|--------|
| `make ESP8266` | `ESP8266/DuetWiFiServer.bin` |
| `make ESP32` | `ESP32/DuetWiFiModule_32.bin` |
| `make ESP32S3` | `ESP32S3/DuetWiFiModule_32S3.bin` |
| `make ESP32C3` | `ESP32C3/DuetWiFiModule_32C3.bin` |
| `make all` | Build all of the above |
| `make clean` | Clean all build outputs |
| `make clean-all` | Clean all build outputs and SDK build artifacts |

Run `make help` for the full list of targets and options. Use `V=1` for verbose output.

SDK paths default to `../ESP8266_RTOS_SDK` and `../esp-idf` and can be overridden:

```console
user@pc:/path/to/WiFiSocketServerRTOS$ make ESP32 ESP_IDF=/path/to/esp-idf
```

### ESP8266 (manual)

#### **Terminal (Linux/macOS)**

1. Install the pre-requisites for your platform: [Linux](https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/get-started/linux-setup.html#install-prerequisites), [macOS](https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/get-started/macos-setup.html#install-prerequisites). Note that you might need additional/different packages depending on your system. For example, on systems  which transitioned to Python 3, you might need `python3-pip python-is-python3 python3-serial` (or the equivalent for your package manager) instead.

2. Clone [ESP8266 RTOS SDK `dwss_support` branch](https://github.com/Duet3D/ESP8266_RTOS_SDK). Make sure the path to the SDK has no spaces.

```console
user@pc:/path/to$ git clone --branch dwss_support --recursive https://github.com/Duet3D/ESP8266_RTOS_SDK.git
```

3. Navigate to the ESP8266 RTOS SDK directory and execute the install script.

```console
user@pc:/path/to/ESP8266_RTOS_SDK$ ./install.sh
```

4. Export environment variables for the current terminal session.


```console
user@pc:/path/to/ESP8266_RTOS_SDK$ . ./export.sh
```

5. Navigate to this directory and execute the `make` command. Exit and save the configuration when prompted. Once the build finishes, `DuetWiFiServer.bin`  will be in the `build` directory.


```console
user@pc:/path/to/WiFiSocketServerRTOS$ make -f Makefile.esp8266
```

#### **Terminal (Windows)**

1. Download the [pre-packaged MSYS environment](https://dl.espressif.com/dl/esp32_win32_msys2_environment_and_toolchain-20181001.zip) and [toolchain](https://dl.espressif.com/dl/xtensa-lx106-elf-gcc8_4_0-esp-2020r3-win32.zip). Extract both to a directory on your system (recommend `C:\` for the pre-packaged MSYS environment, and `C:\msys32\opt` for the toolchain).

2. Clone [ESP8266 RTOS SDK `dwss_support` branch](https://github.com/Duet3D/ESP8266_RTOS_SDK). Make sure the path to the SDK has no spaces.

```console
user@pc /path/to
$ git clone --branch dwss_support --recursive https://github.com/Duet3D/ESP8266_RTOS_SDK.git
```

3. Open a MINGW32 terminal (located at `C:\msys32\mingw32.exe` if the pre-packaged MSYS environment was extracted to `C:\` as recommended).

4. Using the MINGW32 terminal,

    - Set `IDF_PATH` to where the ESP8266 RTOS SDK was extracted and add the toolchain binary directory (`/opt/xtensa-lx106-elf/bin` if the toolchain was extracted to `C:\msys32\opt` as recommended) to `PATH`.

    ```console
    user@pc MINGW32 ~
    $ export IDF_PATH="/path/to/ESP8266_RTOS_SDK"

    user@pc MINGW32 ~
    $ export PATH="/opt/xtensa-lx106-elf/bin":$PATH
    ```

    - Install the Python pre-requisites.

    ```
    user@pc MINGW32 ~
    python -m pip install --user -r $IDF_PATH/requirements.txt
    ```

4. Navigate to this directory and execute the `make` command. Exit and save the configuration when prompted. Once the build finishes, `DuetWiFiServer.bin`  will be in the `build` directory.

```console
user@pc MINGW32 /path/to/WiFiSocketServerRTOS
$ make -f Makefile.esp8266
```


#### **Eclipse**

Follow the instructions for setting up the build environment with Eclipse on [Linux/macOS](https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/get-started/eclipse-setup.html) or [Windows](https://docs.espressif.com/projects/esp8266-rtos-sdk/en/latest/get-started/eclipse-setup-windows.html#eclipse-windows-setup). Make sure to use ESP8266 RTOS SDK `dwss_support` branch.

Afterwards, import this project in the `Import New Project` step.

### ESP32 / ESP32-S3 / ESP32-C3 (manual)

#### **Terminal (Windows/macOS/Linux)**

1. Setup ESP-IDF according to your platform: [Linux/macOS](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/linux-macos-setup.html), [Windows](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/windows-setup.html). Use the Duet3D [esp-idf](https://github.com/Duet3D/esp-idf.git), `dwss_support` branch:
    ```
    git clone --branch dwss_support --recursive https://github.com/Duet3D/esp-idf.git
    ```
    On Windows, choose "`Use an existing ESP-IDF repository`" in the installation wizard, and point it to the resulting clone directory.

2. Navigate to the WiFiSocketServerRTOS directory and execute the following command.  Once the build finishes, `DuetWiFiServer.bin`  will be in the `build` directory.

    ```console
    user@pc:/path/to/WiFiSocketServerRTOS$ idf.py set-target esp32 build
    ```

    Note: Replace `esp32` with the actual target chip

#### **IDE (Windows/macOS/Linux)**

Eclipse and VSCode are supported through plugins. Read more about the plugin setup and build process [on the docs page](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c/get-started/index.html#ide).

## Links

[Forum](https://forum.duet3d.com/)
[Documentation](https://docs.duet3d.com)
