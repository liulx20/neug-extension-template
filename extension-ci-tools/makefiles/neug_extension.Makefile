# NeuG extension build helpers (minimal subset inspired by duckdb/extension-ci-tools).

PROJ_DIR ?= .

# NeuG source resolution (first match wins):
#   1. NEUG_SRCDIR env/override
#   2. ./neug submodule
#   3. ../neug sibling repo (e.g. /data/extensions/neug next to this template)
ifndef NEUG_SRCDIR
  ifneq ($(wildcard $(PROJ_DIR)neug/CMakeLists.txt),)
    NEUG_SRCDIR := $(abspath $(PROJ_DIR)neug)
  else ifneq ($(wildcard $(PROJ_DIR)../neug/CMakeLists.txt),)
    NEUG_SRCDIR := $(abspath $(PROJ_DIR)../neug)
  else
    NEUG_SRCDIR := $(abspath $(PROJ_DIR)neug)
  endif
endif

BUILD_DIR ?= $(PROJ_DIR)build
EXT_NAME ?=
EXT_CONFIG ?= $(PROJ_DIR)extension_config.cmake

BUILD_TYPE ?= Release
BUILD_TEST ?= ON
JOBS ?= $(shell { command -v nproc >/dev/null 2>&1 && nproc; } 2>/dev/null \
              || { command -v sysctl >/dev/null 2>&1 && sysctl -n hw.ncpu; } 2>/dev/null \
              || echo 4)

NEUG_CMAKE_FLAGS := \
	-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) \
	-DBUILD_PYTHON=OFF \
	-DBUILD_TEST=$(BUILD_TEST) \
	-DBUILD_EXTENSIONS=$(EXT_NAME) \
	-DNEUG_EXTENSION_CONFIGS=$(EXT_CONFIG) \
	$(EXTRA_CMAKE_FLAGS)

.PHONY: all release debug build test clean

all: release

release:
	@test -f "$(NEUG_SRCDIR)/CMakeLists.txt" || { echo "NeuG source not found at $(NEUG_SRCDIR). Clone neug as ./neug or ../neug, or set NEUG_SRCDIR."; exit 1; }
	cmake -S $(NEUG_SRCDIR) -B $(BUILD_DIR)/release $(NEUG_CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR)/release -j$(JOBS) --target neug_$(EXT_NAME)_extension
	@if [ "$(BUILD_TEST)" = "ON" ]; then \
		cmake --build $(BUILD_DIR)/release -j$(JOBS) --target $(EXT_NAME)_extension_test; \
	fi

debug:
	@test -f "$(NEUG_SRCDIR)/CMakeLists.txt" || { echo "NeuG source not found at $(NEUG_SRCDIR). Clone neug as ./neug or ../neug, or set NEUG_SRCDIR."; exit 1; }
	cmake -S $(NEUG_SRCDIR) -B $(BUILD_DIR)/debug $(NEUG_CMAKE_FLAGS) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD_DIR)/debug -j$(JOBS) --target neug_$(EXT_NAME)_extension
	@if [ "$(BUILD_TEST)" = "ON" ]; then \
		cmake --build $(BUILD_DIR)/debug -j$(JOBS) --target $(EXT_NAME)_extension_test; \
	fi

build: release

test: release
	cd $(BUILD_DIR)/release && ctest -R $(EXT_NAME)_extension_test -V

clean:
	rm -rf $(BUILD_DIR)
