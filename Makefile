# Compiler and flags
CXX = g++
CXXFLAGS = -std=c++17 -Iinclude
LDFLAGS = -Llib -lllhttp
LIBS_SERVER = -lpq

# Directories and files
SRC_DIR = example
INCLUDE_DIR = include
CLIENT_SRC = $(SRC_DIR)/client.cpp
SERVER_SRC = $(SRC_DIR)/server.cpp
INCLUDE_HEADERS = $(wildcard $(INCLUDE_DIR)/*.h)

# Output binaries
CLIENT_BIN = client
SERVER_BIN = server

# Default target
all: $(CLIENT_BIN) $(SERVER_BIN)

# Build client: depends on its source and all header files
$(CLIENT_BIN): $(CLIENT_SRC) $(INCLUDE_HEADERS)
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) $(LIBS_SERVER) -o $@

# Build server: depends on its source and all header files
$(SERVER_BIN): $(SERVER_SRC) $(INCLUDE_HEADERS)
	$(CXX) $(CXXFLAGS) $< $(LDFLAGS) $(LIBS_SERVER) -o $@

# Clean up
clean:
	rm -f $(CLIENT_BIN) $(SERVER_BIN)
