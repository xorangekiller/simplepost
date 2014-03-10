# SimplePost - A Simple HTTP Server
#
# Copyright (C) 2012-2014 Karl Lenz.  All rights reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have recieved a copy of the GNU General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 021110-1307, USA.

###############################################################################

# Project name
PROJECT_NAME := simplepost

# Target binary to be produced
TARGET := $(PROJECT_NAME).1.gz

# Binary output directory (must include a trailing / if set)
OUTPUT_PATH :=

# Installation directories
PREFIX ?= /usr/local

# Executable definitions
GUNZIP := gzip
RM := rm -f
RMDIR := rmdir
INSTALL := install

###############################################################################

.PHONY: all
all: build

.PHONY: build
build: $(TARGET)

.PHONY: rebuild
rebuild: clean build

.PHONY: install
install:
	$(INSTALL) -d -m 0755 $(DESTDIR)$(PREFIX)/share/man/man1
	$(INSTALL) -m 0644 $(OUTPUT_PATH)$(TARGET) $(DESTDIR)$(PREFIX)/share/man/man1/$(TARGET)

$(TARGET):
	@[ "x$(OUTPUT_PATH)" = "x" ] || $(INSTALL) -d -m 0755 $(OUTPUT_PATH)
	$(GUNZIP) -c $(PROJECT_NAME).1 > $(OUTPUT_PATH)$@

.PHONY: clean
clean:
	$(RM) $(OUTPUT_PATH)$(TARGET)
	-@$(RMDIR) $(OUTPUT_PATH) 2>/dev/null

###############################################################################
