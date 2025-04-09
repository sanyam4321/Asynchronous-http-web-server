CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pedantic -g
TARGET = client
SRCS = client.cpp

# default build rule
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) -o $(TARGET) $(SRCS) $(CXXFLAGS)


clean:
	rm -f $(TARGET)