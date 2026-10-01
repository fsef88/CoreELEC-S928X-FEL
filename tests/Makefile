CC ?= clang
CFLAGS ?= -O2 -Wall

all: test_shaders.exe test_lanczos_math.exe test_dvfel_queue.exe test_memory_viewport.exe test_suite.exe

test_shaders.exe: test_shaders.c
	$(CC) $(CFLAGS) -o $@ $< -lopengl32 -lgdi32

test_lanczos_math.exe: test_lanczos_math.c
	$(CC) $(CFLAGS) -o $@ $< -lm

test_dvfel_queue.exe: test_dvfel_queue.c
	$(CC) $(CFLAGS) -o $@ $<

test_memory_viewport.exe: test_memory_viewport.c
	$(CC) $(CFLAGS) -o $@ $<

test_suite.exe: test_suite.c
	$(CC) $(CFLAGS) -o $@ $<

test: all
	./test_suite.exe

clean:
	rm -f *.exe *.o
