idf_component_register(SRCS "Misc.cpp"
                            "Listener.cpp"
                            "Connection.cpp"
                            "Listener.cpp"
                       INCLUDE_DIRS ".")

add_subdirectory("stubs")
target_link_libraries(${COMPONENT_LIB} PUBLIC stubs)