# WiFiSocketServerRTOS Master Makefile
# Builds firmware for all supported ESP targets.

# Workspace root (parent directory, where SDKs live)
WORKSPACE := ..
export WORKSPACE

# SDK paths (resolved to absolute for use in shell commands)
ESP8266_SDK ?= $(abspath $(WORKSPACE)/ESP8266_RTOS_SDK)
ESP_IDF ?= $(abspath $(WORKSPACE)/esp-idf)

# Quiet build support (Linux kernel style)
# Use V=1 for verbose output
ifeq ($(V),1)
	Q :=
	VERBOSE :=
else
	Q := @
	VERBOSE := -s
endif
export Q VERBOSE

# Parallel jobs for sub-builds
NPROC := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Work around CMake >= 4.0 policy changes for older ESP-IDF
export CMAKE_POLICY_VERSION_MINIMUM := 3.5

# Default target
.DEFAULT_GOAL := help

# Available build configurations
CONFIGS := ESP8266 ESP32 ESP32S3 ESP32C3

# Include board-specific makefiles
-include Makefiles/ESP8266.mk
-include Makefiles/ESP32.mk
-include Makefiles/ESP32S3.mk
-include Makefiles/ESP32C3.mk

# All phony targets
.PHONY: help all clean clean-all \
        $(CONFIGS) $(addprefix clean-,$(CONFIGS))

# Print available targets
help:
	$(Q)echo ""
	$(Q)echo "WiFiSocketServerRTOS Build System"
	$(Q)echo "=================================="
	$(Q)echo ""
	$(Q)echo "Build targets:"
	$(Q)echo "  ESP8266        - Build for ESP8266       -> ESP8266/DuetWiFiServer.bin"
	$(Q)echo "  ESP32          - Build for ESP32         -> ESP32/DuetWiFiModule_32.bin"
	$(Q)echo "  ESP32S3        - Build for ESP32-S3      -> ESP32S3/DuetWiFiModule_32S3.bin"
	$(Q)echo "  ESP32C3        - Build for ESP32-C3      -> ESP32C3/DuetWiFiModule_32C3.bin"
	$(Q)echo ""
	$(Q)echo "Other targets:"
	$(Q)echo "  all            - Build all configurations"
	$(Q)echo "  clean          - Clean all build outputs"
	$(Q)echo "  clean-all      - Clean all build outputs and SDK build artifacts"
	$(Q)echo "  clean-<config> - Clean specific configuration"
	$(Q)echo ""
	$(Q)echo "Environment variables:"
	$(Q)echo "  ESP8266_SDK    - ESP8266 RTOS SDK path (default: $(WORKSPACE)/ESP8266_RTOS_SDK)"
	$(Q)echo "  ESP_IDF        - ESP-IDF SDK path (default: $(WORKSPACE)/esp-idf)"
	$(Q)echo "  V=1            - Enable verbose build output"
	$(Q)echo ""
	$(Q)echo "Examples:"
	$(Q)echo "  make ESP32                                # Build ESP32 firmware"
	$(Q)echo "  make all V=1                              # Build all with verbose output"
	$(Q)echo "  make ESP_IDF=/path/to/esp-idf ESP32       # Custom SDK path"
	$(Q)echo ""

# Build all configurations (sequentially - targets share sdkconfig in project root)
all:
	$(Q)$(MAKE) --no-print-directory ESP8266
	$(Q)$(MAKE) --no-print-directory ESP32
	$(Q)$(MAKE) --no-print-directory ESP32S3
	$(Q)$(MAKE) --no-print-directory ESP32C3

# Generic clean target
clean:
	$(Q)echo "Cleaning all build outputs..."
	$(Q)for config in $(CONFIGS); do \
		if [ -d "$$config" ]; then \
			echo "  RM      $$config"; \
			rm -rf "$$config"; \
		fi; \
	done
	$(Q)rm -f sdkconfig sdkconfig.old
	$(Q)echo "Clean complete"

# Clean all including SDK build artifacts
clean-all: clean
	$(Q)echo "Cleaning SDK build artifacts..."
	$(Q)if [ -d "$(ESP8266_SDK)/build" ]; then \
		echo "  CLEAN   ESP8266_RTOS_SDK"; \
		rm -rf "$(ESP8266_SDK)/build"; \
	fi
	$(Q)if [ -d "$(ESP_IDF)/build" ]; then \
		echo "  CLEAN   esp-idf"; \
		rm -rf "$(ESP_IDF)/build"; \
	fi
	$(Q)echo "Clean all complete"
