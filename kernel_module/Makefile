obj-m += speed_measure.o 
all: 
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 
clean:
	sudmake -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
