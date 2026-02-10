CC      = gcc
CFLAGS  = -O2 -Wall -Wextra
LDFLAGS = -lcurl

TARGET  = pedantixsolver

all: $(TARGET)

wikipedia_bin.o: wikipedia.bin
	ld -r -b binary -o $@ $<

$(TARGET): pedantixsolver.c wikipedia_bin.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET) wikipedia_bin.o

.PHONY: all clean
