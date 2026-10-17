

all:
	gcc -o build.out src/main.c src/map_gen.c -Wall -std=c99 -D_DEFAULT_SOURCE -Wno-missing-braces -Wunused-result -O2 -D_DEFAULT_SOURCE -I. -lraylib -lGL -lm -lpthread -ldl -lrt -lX11 -latomic -DPLATFORM_DESKTOP
	./build.out

debug:
	gcc -o build.out src/main.c src/map_gen.c -g -fsanitize=address -Wall -std=c99 -D_DEFAULT_SOURCE -Wno-missing-braces -Wunused-result -D_DEFAULT_SOURCE -I. -lraylib -lGL -lm -lpthread -ldl -lrt -lX11 -latomic -DPLATFORM_DESKTOP
	./build.out

clean:
	rm build.out

