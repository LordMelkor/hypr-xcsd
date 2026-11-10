# hyprpm supplies PKG_CONFIG_PATH for its ABI-matched Hyprland headers.
CXX ?= c++
PKG_CONFIG ?= pkg-config
CPPFLAGS += $(shell $(PKG_CONFIG) --cflags hyprland pixman-1 libdrm xcb)
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++26 -fPIC -Wall -Wno-unused-parameter
LDFLAGS += -shared

all: hypr-xcsd.so

hypr-xcsd.so: main.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f hypr-xcsd.so

.PHONY: all clean
