CC = gcc
CFLAGS = -O2 -Wall -W
LDFLAGS =
OBJS = statvfs.o
TARGET = statvfs

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $< -o $@

clean:
	$(RM) $(OBJS) $(TARGET)
