CC=$(RETRO68)/bin/m68k-apple-macos-g++
REZ=$(RETRO68)/bin/Rez
CFLAGS=-Os
LDFLAGS=-lRetroConsole
RINCLUDES=$(RETRO68)/m68k-apple-macos/RIncludes
REZFLAGS=-I$(RINCLUDES)

ASCTester.bin: ASCTester.code.bin
	$(REZ) $(REZFLAGS) \
		--copy "ASCTester.code.bin" \
		"$(RINCLUDES)/Retro68APPL.r" \
		-t "APPL" -c "????" \
		-o ASCTester.bin --cc "._ASCTester.ad" --cc ASCTester.dsk

ASCTester.code.bin: tests.o
	$(CC) $^ -o $@ $(LDFLAGS)

.PHONY: clean
clean:
	rm -f ASCTester.bin ASCTester.code.bin ASCTester.code.bin.gdb tests.o ASCTester.ad ._ASCTester.ad ASCTester.dsk

.PHONY: test
test:
	LaunchAPPL ASCTester.bin
