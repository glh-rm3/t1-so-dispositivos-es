main: main.o scan_device.o
	cc main.o scan_device.o -o main

main.o: src/main.c
	cc -c src/main.c -I./include

scan_device.o: src/scan_device.c
	cc -c src/scan_device.c -I./include

clean:
	rm -f *.o main
