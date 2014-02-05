
all: clean
	make -C module default
	make -C pts all

clean:
	make -C module clean
	make -C pts clean
