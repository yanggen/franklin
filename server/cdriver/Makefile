# Makefile - cdriver build rules for Franklin
# Copyright 2014 Michigan Technological University
# Author: Bas Wijnen <wijnen@debian.org>
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Affero General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Affero General Public License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

#TARGET ?= bbb

PROFILE = #-pg

CPPFLAGS ?= -g -Wformat -Werror=format-security -D_FORTIFY_SOURCE=2 -Wshadow $(PROFILE)
LDFLAGS ?= $(PROFILE)

all: franklin-cdriver

ifeq (${TARGET}, bbb)
ARCH_HEADER = arch-bbb.h
LIBS += -lprussdrv

%.bin: %.asm Makefile
	pasm -V2 -b $<

franklin-cdriver: bbb_pru.bin
else
ARCH_HEADER = arch-avr.h
endif

SOURCES = \
	base.cpp \
	debug.cpp \
	globals.cpp \
	gpio.cpp \
	hostserial.cpp \
	move.cpp \
	packet.cpp \
	run.cpp \
	serial.cpp \
	setup.cpp \
	space.cpp \
	storage.cpp \
	temp.cpp \
	type-cartesian.cpp \
	type-delta.cpp \
	type-polar.cpp

HEADERS = \
	configuration.h \
	cdriver.h \
	${ARCH_HEADER}

CPPFLAGS += -DARCH_INCLUDE=\"${ARCH_HEADER}\"

OBJECTS = $(addprefix build/,$(patsubst %.cpp,%.o,$(SOURCES)))

franklin-cdriver: $(OBJECTS) Makefile
	g++ $(LDFLAGS) $(OBJECTS) $(LIBS) -o $@

build/stamp:
	mkdir -p build
	touch $@

build/%.o: %.cpp $(HEADERS) build/stamp Makefile
	g++ $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJECTS) build franklin-cdriver $(DTBO)
