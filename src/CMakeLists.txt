set(srcs "Misc.cpp"
         "Listener.cpp"
         "SocketServer.cpp"
         "HSPI.cpp"
         "Connection.cpp")
set(include_dirs ".")

if(IDF_TARGET STREQUAL "esp8266")
     list(APPEND srcs "esp8266/gpio.c")
     list(APPEND priv_include_dirs "esp8266")
endif()

idf_component_register(SRCS "${srcs}"
                       PRIV_INCLUDE_DIRS "${priv_include_dirs}"
                       REQUIRES spi_flash mdns)

add_subdirectory("stubs")
target_link_libraries(${COMPONENT_LIB} PUBLIC stubs)

set_source_files_properties("SocketServer.cpp" PROPERTIES COMPILE_FLAGS "-Wno-error=nonnull")

partition_table_get_partition_info(ssids_size "--partition-name ssids" "size")
partition_table_get_partition_info(ssids_offset "--partition-name ssids" "offset")

set(ssid_init ${build_dir}/ssid_init.bin)
idf_build_get_property(idf_path IDF_PATH)
idf_build_get_property(python PYTHON)
add_custom_command(OUTPUT ${ssid_init}
     COMMAND ${python} ${idf_path}/components/partition_table/gen_empty_partition.py
     ${ssids_size} ${ssid_init})

add_custom_target(ssid_init ALL DEPENDS ${ssid_init})
add_dependencies(flash ssid_init)

esptool_py_flash_project_args(ssids ${ssids_offset} "${ssid_init}")