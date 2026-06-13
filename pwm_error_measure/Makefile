# compiler path can be added to PATH by export 
CC = arm-none-linux-gnueabihf-gcc
# if libgpiod is placed in the same place of the application folder (figure)
INCLUDES = -I../libgpiod-arm/include
LIB = -L../libgpiod-arm/lib
CFLAGS	= -Wall
BIN_DIR = ./build
# if customlib is placed in the same place of the application folder (figure)
CUSTOM = ../customlib/*.o
LINKLIB = -lgpiod -lpthread

PWM: PWM.c
	$(CC) $(CFLAGS) -o $(BIN_DIR)/PWM $(CUSTOM) PWM.c \
	$(INCLUDES)  $(LIB) $(LINKLIB)
.PHONY: clean
clean:
	rm -f $(BIN_DIR)/PWM

#.
#├── customlib
#│   ├── keybdetect.o
#│   └── what_inside.txt
#├── PWM
#│   ├── build
#│   │   └── PWM
#│   ├── PWM.c
#│   └── Makefile
#└── libgpiod
#    └── rootfs
#        ├── include
#        │   └── gpiod.h
#        └── lib
#            ├── libgpiod.a
#            ├── libgpiod.la
#            ├── libgpiod.so -> libgpiod.so.2.2.2
#            ├── libgpiod.so.2 -> libgpiod.so.2.2.2
#            ├── libgpiod.so.2.2.2
#            └── pkgconfig
#                └── libgpiod.pc


