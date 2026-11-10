# hyprpm supplies PKG_CONFIG_PATH for its ABI-matched Hyprland headers.
CXX ?= c++
PKG_CONFIG ?= pkg-config
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags hyprland pixman-1 libdrm xcb)
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++26 -fPIC -Wall -Wno-unused-parameter
LDFLAGS += -shared

all: hypr-xcsd.so

hypr-xcsd.so: main.cpp decoration-policy.hpp hook-signatures.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $< -o $@

# Checks that need no running compositor. HYPRLAND selects the binary whose
# hook symbols are verified. KEEP_BELOW_BAR=<path> also runs the Lua checks
# against an Omarchy keep-below-bar module.
HYPRLAND ?= /usr/bin/Hyprland
test: hypr-xcsd.so tests/decoration-policy
	./tests/decoration-policy
	python3 tests/maintenance.py
	scripts/check-hook-symbols $(HYPRLAND)
	@if [ -n "$(KEEP_BELOW_BAR)" ]; then lua tests/bar-correction.lua "$(KEEP_BELOW_BAR)"; fi

tests/decoration-policy: tests/decoration-policy.cpp decoration-policy.hpp
	$(CXX) -std=c++20 -O1 $< -o $@

clean:
	rm -f hypr-xcsd.so tests/decoration-policy

.PHONY: all test clean
