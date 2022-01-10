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
                       PRIV_INCLUDE_DIRS "${priv_include_dirs}")

add_subdirectory("stubs")
target_link_libraries(${COMPONENT_LIB} PUBLIC stubs)

idf_component_get_property(lwip_lib lwip COMPONENT_LIB)
target_compile_definitions(${lwip_lib} PUBLIC "LWIP_MDNS_RESPONDER=1")

set_source_files_properties("SocketServer.cpp" PROPERTIES COMPILE_FLAGS "-Wno-error=nonnull")