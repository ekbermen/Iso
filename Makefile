TARGET = MiniArcade
OBJS = main.o
BUILD_PRX = 1
CFLAGS = -O2 -G0 -Wall
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)
LIBS = -lpspaudio -lm
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Mini Arcade
PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
