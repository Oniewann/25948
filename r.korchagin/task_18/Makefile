CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
TARGET = my_ls
SRC = my_ls.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)
