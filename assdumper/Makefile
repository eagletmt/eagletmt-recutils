CXX = g++
CXXFLAGS = -O2 -Wall -W
LDFLAGS =
OBJS = assdumper.o
TARGET = assdumper

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) $< -o $@

clean:
	$(RM) $(OBJS) $(TARGET)
