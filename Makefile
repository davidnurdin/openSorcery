# Sorcery+ (CPC 6128) - two builds:
#   make emu      : version A - original Z80 code run by an emulator core + CPC firmware shims
#   make native   : version B - native C++ port (work in progress, see native/README.md)
# SDL2: uses pkg-config, or pass SDL_INC="-I.../SDL2" SDL_LIB=".../libSDL2-2.0.so.0"
CXX      ?= g++
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wno-unused-parameter
SDL_INC  ?= $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIB  ?= $(shell pkg-config --libs sdl2 2>/dev/null)
EMU_SRC   = emu/main.cpp emu/machine.cpp emu/z80.cpp emu/sound.cpp common/dsk.cpp
NAT_SRC   = native/src/main.cpp native/src/machine.cpp native/src/sound.cpp native/src/alu.cpp native/src/native.cpp $(wildcard native/src/port_*.cpp) common/resources.cpp common/png_io.cpp common/roomtext.cpp

all: emu native
emu: sorcery-emu sorcery-emu-headless
sorcery-emu: $(EMU_SRC) emu/*.h common/*.h
	$(CXX) $(CXXFLAGS) -DUSE_SDL $(SDL_INC) -o $@ $(EMU_SRC) $(SDL_LIB)
sorcery-emu-headless: $(EMU_SRC) emu/*.h common/*.h
	$(CXX) $(CXXFLAGS) -o $@ $(EMU_SRC)
native: sorcery-native sorcery-native-headless
sorcery-native: $(NAT_SRC) native/src/*.h common/*.h
	$(CXX) $(CXXFLAGS) -DUSE_SDL $(SDL_INC) -o $@ $(NAT_SRC) $(SDL_LIB)
sorcery-native-headless: $(NAT_SRC) native/src/*.h common/*.h
	$(CXX) $(CXXFLAGS) -o $@ $(NAT_SRC)
make_resources: tools/make_resources.cpp common/*.cpp common/*.h
	$(CXX) $(CXXFLAGS) -o $@ tools/make_resources.cpp common/resources.cpp common/png_io.cpp common/roomtext.cpp common/dsk.cpp
zex: tests/zex.cpp emu/z80.cpp emu/z80.h
	$(CXX) -O2 -std=c++17 -o $@ tests/zex.cpp emu/z80.cpp
clean:
	rm -f make_resources sorcery-emu sorcery-emu-headless sorcery-native sorcery-native-headless zex
