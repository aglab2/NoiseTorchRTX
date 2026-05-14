default:
	$(CC) -I -Wall -Werror -ggdb -O3 -march=native -flto -c -fPIC ../c-ringbuf/ringbuf.c module.c maxine_effect.c maxine_loader.c
	$(CC) -o rnnoise_ladspa.so *.o -shared -Wl,--version-script=export.txt -lm
