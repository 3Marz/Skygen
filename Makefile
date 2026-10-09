

all:
	gcc -o build src/main.c -Wall -std=c99 -D_DEFAULT_SOURCE -Wno-missing-braces -Wunused-result -O2 -D_DEFAULT_SOURCE -I. -lraylib -lGL -lm -lpthread -ldl -lrt -lX11 -latomic -DPLATFORM_DESKTOP
	./build

clean:
	rm build 

