CFLAGS = -I -Wall -Werror -ggdb -O3 -march=native -flto
LDFLAGS=-Wl,-rpath,\$$ORIGIN -Wl,-z,origin

default:
	$(CC) $(CFLAGS) -c -fPIC ../c-ringbuf/ringbuf.c module.c maxine_effect.c maxine_loader.c
	$(CC) -o rnnoise_ladspa.so *.o -shared -Wl,--version-script=export.txt -lm
	$(CC) $(CFLAGS) $(LDFLAGS) standalone.c -o standalone
