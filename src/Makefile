# SimplePost - A Basic, Embedded HTTP Server
#
# Copyright (C) 2012-2013 xorangekiller.  All rights reserved.
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
PROJECT_NAME = simplepost

# Target binary to be produced
TARGET = $(PROJECT_NAME)

# List of C source files
CSRCS =

# List of C++ source files
CXXSRCS = simplepost.cpp \
		  simplemain.cpp

# List of the objects files to be made
OBJECTS = $(CSRCS:.c=.o) $(CXXSRCS:.cpp=.o)

# Binary output directory (must include a trailing / if set)
OUTPUT_PATH = ../bin/

# Application optimization used during compilation and linking:
# -O0, -O1, -O2, -O3 or -Os
ifdef DEBUG
OPTIMIZATION = -O0
else
OPTIMIZATION = -O2
endif

# List of include paths
INCLUDES =

# List of search paths for libraries
LIB_PATH =

# List of libraries to use during linking
LIBS = -lpthread

# C compiler options
CFLAGS += -xc -c -std=c99
CFLAGS += $(OPTIMIZATION)
CFLAGS += -DSIMPLEPOST
ifdef DEBUG
CFLAGS += -DDEBUG
CFLGAS += -g -Wall -Werror
CFLGAS += -fno-inline -fno-diagnostics-show-option
else
CFLAGS += -w
endif

# C++ compiler options
CXXFLAGS += -xc++ -c -std=c++03
CXXFLAGS += $(OPTIMIZATION)
CXXFLAGS += -DSIMPLEPOST
ifdef DEBUG
CXXFLAGS += -DDEBUG
CXXFLGAS += -g -Wall -Werror
CXXFLAGS += -pedantic-errors
CXXFLGAS += -fno-inline -fno-diagnostics-show-option
else
CXXFLAGS += -w
endif

# Linker options
LDFLAGS += $(OPTIMIZATION) $(LIB_PATH)
ifndef DEBUG
LDFLAGS += -s
endif

# Executable definitions
CC := gcc
CXX := g++
OBJDUMP := objdump
SIZE := size
RM := rm -rf

###############################################################################

.PHONY: all
all: build

.PHONY: build
build: $(TARGET)

.PHONY: rebuild
rebuild: clean build

$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) $(OBJECTS) $(LIBS) -o $(OUTPUT_PATH)$@

.PHONY: stats
stats: $(TARGET)
	$(OBJDUMP) -th $(OUTPUT_PATH)$<
	$(SIZE) $(OUTPUT_PATH)$<

.PHONY: clean
clean:
	$(RM) $(OBJECTS)
	$(RM) $(OUTPUT_PATH)$(TARGET)

###############################################################################

.c.o:
	$(CC) $(CFLAGS) $< -o $@

.cpp.o:
	$(CXX) $(CXXFLAGS) $< -o $@

###############################################################################
