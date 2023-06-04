ALL: run

CC := arm-none-linux-gnueabihf-gcc


run: main.o
	$(CC) $^ -o $@
	cp $@ ~/projects/pkgs/qemu-env-tst/tmp

.PHONY: clean
clean:
	-rm *.o run
