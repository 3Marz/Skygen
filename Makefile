

all:
	gcc -o build.out src/main.c src/map_gen.c src/map_gen_height.c src/utils.c -Wall -std=c99 -D_DEFAULT_SOURCE -Wno-missing-braces -Wunused-result -D_DEFAULT_SOURCE -I. -lraylib -lGL -lm -lpthread -ldl -lrt -lX11 -latomic -DPLATFORM_DESKTOP
	./build.out

debug:
	gcc -o build.out src/main.c src/map_gen.c src/map_gen_height.c src/utils.c -g -fsanitize=address -Wall -std=c99 -D_DEFAULT_SOURCE -Wno-missing-braces -Wunused-result -D_DEFAULT_SOURCE -I. -lraylib -lGL -lm -lpthread -ldl -lrt -lX11 -latomic -DPLATFORM_DESKTOP
	./build.out

clean:
	rm build.out

