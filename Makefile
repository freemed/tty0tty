
all: clean
	make -C module all
	make -C pts all

clean:
	make -C module clean
	make -C pts clean
