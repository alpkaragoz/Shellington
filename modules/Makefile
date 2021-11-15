obj-m += pstraverse_module.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
test:
	sudo dmesg -C
	sudo insmod pstraverse_module.ko user_input=4248
	sudo rmmod pstraverse_module.ko
	dmesg