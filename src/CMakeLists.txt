set(CMAKE_CXX_STANDARD 17)

set(srcs "Misc.cpp"
         "Listener.cpp"
         "SocketServer.cpp"
         "HSPI.cpp"
         "Connection.cpp"
         "DNSServer.cpp")
set(include_dirs ".")

if(IDF_TARGET STREQUAL "esp8266")
     list(APPEND srcs "esp8266/gpio.c")
     list(APPEND priv_include_dirs "esp8266")
endif()

idf_component_register(SRCS "${srcs}"
                       PRIV_INCLUDE_DIRS "${priv_include_dirs}"
                       REQUIRES spi_flash mdns)

idf_build_get_property(python PYTHON)
idf_build_get_property(build_dir BUILD_DIR)

add_custom_target(image ALL DEPENDS app bootloader ${ssid_init}
     COMMAND ${python} "${CMAKE_CURRENT_LIST_DIR}/create_image.py"
                    "${esptool_dir}" "${build_dir}" "${build_dir}/DuetWiFiServer.bin")
