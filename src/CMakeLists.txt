idf_component_register(SRCS "Misc.cpp"
                            "Listener.cpp"
                            "Connection.cpp"
                            "SocketServer.cpp"
                            "HSPI.cpp"
                       INCLUDE_DIRS ".")

add_subdirectory("stubs")
target_link_libraries(${COMPONENT_LIB} PUBLIC stubs)

idf_component_get_property(lwip_lib lwip COMPONENT_LIB)
target_compile_definitions(${lwip_lib} PUBLIC "LWIP_MDNS_RESPONDER=1")

set_source_files_properties("SocketServer.cpp" PROPERTIES COMPILE_FLAGS "-Wno-error=nonnull")