#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := WiFiSocketServerRTOS

EXTRA_COMPONENT_DIRS = src
include $(IDF_PATH)/make/project.mk
