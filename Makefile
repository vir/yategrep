CFLAGS?=-O2

.PHONY: clean

.cpp.o: $<
	g++ -Wall $(CFLAGS) -I`yate-config --includes` $(DEBUG) -Wno-overloaded-virtual -fno-exceptions -DHAVE_GCC_FORMAT_CHECK -DHAVE_BLOCK_RETURN -I/usr/include/yate -c -o $@ $^

all: yategrep

yategrep: yategrep.o
	g++ $(DEBUG) -lyate -o $@ $^

yategrep.o: yategrep.cpp

clean:
	rm -f $(patsubst %.cpp,%.o,$(wildcard *.cpp)) yategrep

debug:
	$(MAKE) all DEBUG=-g3 MODSTRIP= CFLAGS=

ddebug:
	$(MAKE) all DEBUG='-g3 -DDEBUG' MODSTRIP= CFLAGS=

xdebug:
	$(MAKE) all DEBUG='-g3 -DXDEBUG' MODSTRIP= CFLAGS=

ndebug:
	$(MAKE) all DEBUG='-g0 -DNDEBUG'


