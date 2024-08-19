TARGET = caen-felib-demo-dpp-zle
CFLAGS = -std=gnu11 -O2 -g -Wall -pthread
LDFLAGS = -pthread
LDLIBS = -lpthread -lCAEN_FELib

.PHONY: default all clean

default: $(TARGET)
all: default

OBJECTS = main.o

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

clean:
	-rm -f *.o
	-rm -f $(TARGET)
