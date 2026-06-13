CC = arm-none-linux-gnueabihf-gcc
INCLUDES = -I../libgpiod-arm/include
LIB = -L../libgpiod-arm/lib
CFLAGS = -Wall
BIN_DIR = ./build
CUSTOM = ../customlib/*.o
LINKLIB = -lpthread        # ← removed -lgpiod, not needed

led_pwm_control: led_pwm_control.c
	$(CC) $(CFLAGS) -o $(BIN_DIR)/led_pwm_control $(CUSTOM) led_pwm_control.c \
	$(INCLUDES) $(LIB) $(LINKLIB)

.PHONY: clean
clean:
	rm -f $(BIN_DIR)/led_pwm_control
