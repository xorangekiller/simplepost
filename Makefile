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

.PHONY: all
all:
	$(MAKE) -C src -f Makefile $@

.PHONY: build
build:
	$(MAKE) -C src -f Makefile $@

.PHONY: rebuild
rebuild:
	$(MAKE) -C src -f Makefile $@

.PHONY: install
install:
	$(MAKE) -C src -f Makefile $@

.PHONY: stats
stats:
	$(MAKE) -C src -f Makefile $@

.PHONY: clean
clean:
	$(MAKE) -C src -f Makefile $@
