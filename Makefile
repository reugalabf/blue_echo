# Variables
CC = gcc
CFLAGS = -Wall -Wextra -O2
LIBS = -lbluetooth
TARGET = bt_echo_server
SRC = bt_echo_server.c

# Default target
all: $(TARGET)

# Compile target
$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LIBS)

# Clean up build files
clean:
	rm -f $(TARGET)

.PHONY: all clean
