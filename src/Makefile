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

# Executable definitions
ifndef CC
CC=gcc
endif
ifndef CXX
CXX=g++
endif
RM = rm -rf

# Compiler definitions
ALLCDEFS = -DSIMPLEPOST
RELCDEFS = $(ALLCDEFS)
DBGCDEFS = $(ALLCDEFS) -DDEBUG

# Compiler flags
ALLCFLAGS = -c
DBGCFLAGS = $(ALLCFLAGS) -g -O0 -fno-inline -fno-diagnostics-show-option
RELCFLAGS = $(ALLCFLAGS) -w -O2 -xc++

# Compiler include directories
ALLCIDIR =
RELCIDIR = $(ALLCIDIR)
DBGCIDIR = $(ALLCIDIR)

# Aggregated compiler flags
RELCFINAL = $(RELCFLAGS) $(RELCDEFS) $(RELCIDIR)
DBGCFINAL = $(DBGCFLAGS) $(DBGCDEFS) $(DBGCIDIR)
# IMPORTANT: Compiler flags below!
ifneq "$(findstring debug, $(MAKECMDGOALS))" ""
CFINAL = $(DBGCFINAL)
else
CFINAL = $(RELCFINAL)
endif

# Linked libraries
ALLLLIB = -lpthread
RELLLIB = $(ALLLLIB)
DBGLLIB = $(ALLLLIB)

# Linker flags
ALLLFLAGS =
RELLFLAGS = $(ALLLFLAGS) -s
DBGLFLAGS = $(ALLLFLAGS)

# Linker include directories
ALLLIDIR =
RELLIDIR = $(ALLLIDIR)
DBGLIDIR = $(ALLLIDIR)

# Aggregated linker flags
RELLFINAL = $(RELLFLAGS) $(RELLLIB) $(RELLIDIR)
DBGLFINAL = $(DBGLFLAGS) $(DBGLLIB) $(DBGLIDIR)
# IMPORTANT: Linker flags below!
ifneq "$(findstring debug, $(MAKECMDGOALS))" ""
LNFINAL = $(DBGLFINAL)
else
LNFINAL = $(RELLFINAL)
endif

# Source files and objects
SOURCES = simplepost.cpp \
		  simplemain.cpp
OBJECTS = $(SOURCES:.cpp=.o)

# Binary
OUTDIR = ../bin
BINARY = $(OUTDIR)/simplepost

# The target definitions and commands are the only thing below this line.
# You should not need to modify anything from here down!

.PHONY: all debug \
	clean clean_src clean_bin

all: $(SOURCES) $(BINARY)

debug: $(SORUCES) $(BINARY)

$(BINARY): $(OBJECTS)
	$(CXX) -o "$(BINARY)" $(OBJECTS) $(LNFINAL)

.cpp.o:
	$(CXX) $(CFINAL) $< -o $@

clean: clean_src clean_bin

clean_src:
	-@$(RM) $(OBJECTS) $(RESOBJ)

clean_bin:
	-@$(RM) $(BINARY)
