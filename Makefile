CC = cc
CFLAGS = -O2 -Wall
TARGET = writefile

# AIX specific flags
ifeq ($(shell uname),AIX)
    CFLAGS += -D_AIX -D_LARGE_FILES
endif

all: $(TARGET)

$(TARGET): writefile.c
	$(CC) $(CFLAGS) -o $(TARGET) writefile.c

clean:
	rm -f $(TARGET) $(TARGET).exe

test: $(TARGET)
	./$(TARGET) 100M test100m.dat
	ls -lh test100m.dat
	rm -f test100m.dat

.PHONY: all clean test