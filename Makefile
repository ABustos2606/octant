# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) 2026 Alejandro Bustos

PREFIX ?= /usr/local
LV2DIR ?= $(PREFIX)/lib/lv2
DESTDIR ?=
OPT ?= -O2
PYTHON ?= python3
PKG_CONFIG ?= pkg-config
BUILD_DIR ?= build

BUNDLE := $(BUILD_DIR)/octant.lv2
METADATA := manifest.ttl octant.ttl
TTL_TARGETS := $(addprefix $(BUNDLE)/,$(METADATA))
DSP := src/bank.hpp src/voices.hpp
LV2_CFLAGS = $(shell $(PKG_CONFIG) --cflags lv2)
# -fno-fast-math comes after OPT: the phase tracking and the non-finite guards
# need IEEE arithmetic even if a host build passes -ffast-math in OPT.
COMPILE_FLAGS = $(CPPFLAGS) $(LV2_CFLAGS) $(CXXFLAGS) $(OPT) -fno-fast-math -std=c++17 -fPIC -fvisibility=hidden -Wall -Wextra

.PHONY: all install test render lint clean
all: $(BUNDLE)/octant.so $(TTL_TARGETS)

$(BUNDLE)/octant.so: src/lv2_entry.cpp $(DSP) src/ports.hpp src/exports.map
	@mkdir -p "$(@D)"
	$(CXX) $(COMPILE_FLAGS) src/lv2_entry.cpp $(LDFLAGS) -shared -Wl,--no-undefined -Wl,--version-script=src/exports.map -o $@

$(BUNDLE)/%.ttl: octant.lv2/%.ttl
	@mkdir -p "$(@D)"
	install -m 644 $< $@

$(BUILD_DIR)/test_bank: tests/test_bank.cpp src/bank.hpp
	@mkdir -p "$(@D)"
	$(CXX) $(COMPILE_FLAGS) $< $(LDFLAGS) -o $@

$(BUILD_DIR)/test_voices: tests/test_voices.cpp $(DSP)
	@mkdir -p "$(@D)"
	$(CXX) $(COMPILE_FLAGS) $< $(LDFLAGS) -o $@

$(BUILD_DIR)/test_lv2: tests/test_lv2.cpp src/ports.hpp
	@mkdir -p "$(@D)"
	$(CXX) $(COMPILE_FLAGS) $< $(LDFLAGS) -ldl -o $@

$(BUILD_DIR)/render: tools/render.cpp src/ports.hpp
	@mkdir -p "$(@D)"
	$(CXX) $(COMPILE_FLAGS) $< $(LDFLAGS) -ldl -o $@

render: all $(BUILD_DIR)/render

test: all $(BUILD_DIR)/test_bank $(BUILD_DIR)/test_voices $(BUILD_DIR)/test_lv2
	$(PYTHON) tests/ttl_check.py $(BUNDLE)
	$(BUILD_DIR)/test_bank
	$(BUILD_DIR)/test_voices
	$(BUILD_DIR)/test_lv2 $(BUNDLE)/octant.so

install: all
	install -d "$(DESTDIR)$(LV2DIR)/octant.lv2"
	install -m 755 $(BUNDLE)/octant.so "$(DESTDIR)$(LV2DIR)/octant.lv2/"
	install -m 644 $(TTL_TARGETS) LICENSE "$(DESTDIR)$(LV2DIR)/octant.lv2/"

lint:
	@if command -v lv2lint >/dev/null 2>&1; then \
		LV2_PATH="$(DESTDIR)$(LV2DIR):$${LV2_PATH:-/usr/local/lib/lv2:/usr/lib/lv2}" lv2lint 'https://github.com/ABustos2606/octant#octant'; \
	else echo 'SKIP lv2lint: not installed (install the bundle with make install first)'; fi

clean:
	$(RM) -r $(BUILD_DIR)
