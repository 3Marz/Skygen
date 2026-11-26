
src_files = src/main.c src/map_gen.c src/map_gen_height.c src/utils.c src/height_field.c src/terrain.c src/generation_ctx.c include/hashmap.c

ifdef OS
	RM = del
	safe_path = $(subst /,\,$1)
	libs = -I. -Iinclude -Llib lib\libr3d.a lib\libraylib.a -lgdi32 -lwinmm -lpthread -lopengl32 -lglu32
	build_file = build.exe
	run_path = .\$(build_file)
else
	RM = rm
	safe_path = $1
	libs = -I. -lr3d -lraylib -lGL -lm -lpthread -ldl -lrt -lX11 -latomic
	build_file = build.out
	run_path = ./$(build_file)
endif

.PHONY: all build debug clean

all:
	gcc -o $(build_file) $(call safe_path,$(src_files)) -O3 -Wall -std=c99 -D_DEFAULT_SOURCE -Wno-missing-braces -Wunused-result -D_DEFAULT_SOURCE $(libs) -DPLATFORM_DESKTOP
	$(run_path)

build:
	gcc -o $(build_file) $(call safe_path,$(src_files)) -Wall -std=c99 -D_DEFAULT_SOURCE -Wno-missing-braces -Wunused-result -D_DEFAULT_SOURCE $(libs) -DPLATFORM_DESKTOP

debug:
	gcc -o $(build_file) $(call safe_path,$(src_files)) -g -fsanitize=address -Wall -std=c99 -D_DEFAULT_SOURCE -Wno-missing-braces -Wunused-result -D_DEFAULT_SOURCE $(libs) -DPLATFORM_DESKTOP
	$(run_path)

clean:
	$(RM) $(build_file)

