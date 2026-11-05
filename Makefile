
src_files = src/main.c src/map_gen.c src/map_gen_height.c src/utils.c src/terrain.c src/generation_ctx.c include/sds.c
build_file = build.out

ifdef OS
	RM = del
	safe_path = $(subst /,\,$1)
	libs = -I. -Iinclude -Llib -lraylib -lgdi32 -lwinmm
	run_path = .\$(build_file)
else
	RM = rm
	safe_path = $1
	libs = -I. -lraylib -lGL -lm -lpthread -ldl -lrt -lX11 -latomic
	run_path = ./$(build_file)
endif

all:
	gcc -o $(build_file) $(call safe_path,$(src_files)) -Wall -std=c99 -D_DEFAULT_SOURCE -Wno-missing-braces -Wunused-result -D_DEFAULT_SOURCE $(libs) -DPLATFORM_DESKTOP
	$(run_path)

debug:
	gcc -o $(build_file) $(call safe_path,$(src_files)) -g -fsanitize=address -Wall -std=c99 -D_DEFAULT_SOURCE -Wno-missing-braces -Wunused-result -D_DEFAULT_SOURCE $(libs) -DPLATFORM_DESKTOP
	$(run_path)

clean:
	$(RM) $(build_file)

