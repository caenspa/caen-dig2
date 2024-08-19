TARGET = caen-felib-demo-scope
CFLAGS = -std=gnu11 -O2 -g -Wall -pthread
LDFLAGS = -pthread
LDLIBS = -lpthread -lm -lCAEN_FELib

.PHONY: default all clean

default: $(TARGET)
all: default

OBJECTS = main.o tlock_queue.c

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) $(LDLIBS) -o $@

clean:
	-rm -f *.o
	-rm -f $(TARGET)
