# Builds hypr-xcsd against the installed Hyprland headers.
#
# Output: $(OUTDIR)/<hyprland-commit>-<source-hash>.so, plus a pointer file
# $(OUTDIR)/<hyprland-commit>.current holding that filename (what the loader
# reads). The commit means a build is never loaded into a different Hyprland.
# The source hash gives every build a new path: the plugin can't truly be
# unloaded (it stays mapped), and loading an already-mapped path again returns
# the old code, so a rebuilt plugin must never reuse a path.
HDR     := /usr/include/hyprland/src/version.h
HASH    := $(shell sed -n 's/^\#define GIT_COMMIT_HASH *"\(.*\)"/\1/p' $(HDR))
SRCHASH := $(shell sha256sum main.cpp | cut -c1-8)
OUTDIR  ?= $(HOME)/.local/lib/hypr-xcsd
SO      := $(OUTDIR)/$(HASH)-$(SRCHASH).so
CXXFLAGS = -shared -fPIC -O2 -std=c++26 -Wall -Wno-unused-parameter $(shell pkg-config --cflags hyprland pixman-1 libdrm xcb)

all: $(SO)
	echo $(notdir $(SO)) > $(OUTDIR)/$(HASH).current.tmp
	mv $(OUTDIR)/$(HASH).current.tmp $(OUTDIR)/$(HASH).current

$(SO): main.cpp $(HDR)
	@mkdir -p $(OUTDIR)
	$(CXX) $(CXXFLAGS) main.cpp -o $@.tmp
	mv $@.tmp $@

hash:
	@echo $(HASH)

.PHONY: all hash
