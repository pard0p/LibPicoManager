CC=i686-w64-mingw32-gcc
CC_64=x86_64-w64-mingw32-gcc

all: libpicomanager.x86.zip libpicomanager.x64.zip

bin:
	mkdir -p Bin

#
# x86 targets
#
libpicomanager.x86.zip: bin
	$(CC) -DWIN_X86 -shared -masm=intel -Wall -Wno-pointer-arith -c Source/PicoManager.c -o Bin/PicoManager.x86.o
	$(CC) -DWIN_X86 -shared -masm=intel -Wall -Wno-pointer-arith -c Source/picorun.c     -o Bin/picorun.x86.o
	zip -q -j LibPicoManager.x86.zip Bin/*.x86.o

#
# x64 targets
#
libpicomanager.x64.zip: bin
	$(CC_64) -DWIN_X64 -shared -masm=intel -Wall -Wno-pointer-arith -c Source/PicoManager.c -o Bin/PicoManager.x64.o
	$(CC_64) -DWIN_X64 -shared -masm=intel -Wall -Wno-pointer-arith -c Source/picorun.c     -o Bin/picorun.x64.o
	zip -q -j LibPicoManager.x64.zip Bin/*.x64.o

#
# Other targets
#
clean:
	rm -rf Bin/*.o
	rm -f LibPicoManager.x86.zip
	rm -f LibPicoManager.x64.zip