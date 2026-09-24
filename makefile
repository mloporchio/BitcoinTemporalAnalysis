#
#	File:	makefile
#	Author:	Matteo Loporchio
#

CXX=g++
CXXFLAGS=-O3 -march=native -std=c++17 -pthread -Wall
SRC_DIR=src
BIN_DIR=bin

# Create output directories
$(shell mkdir -p $(BIN_DIR))

.PHONY: all clean

all: $(BIN_DIR)/pg_el_builder $(BIN_DIR)/edge_sorter

$(BIN_DIR)/pg_el_builder: $(SRC_DIR)/pg_el_builder.cpp
	$(CXX) $(CXXFLAGS) -o $(BIN_DIR)/pg_el_builder $(SRC_DIR)/pg_el_builder.cpp

$(BIN_DIR)/edge_sorter: $(SRC_DIR)/edge_sorter.cpp
	$(CXX) $(CXXFLAGS) -o $(BIN_DIR)/edge_sorter $(SRC_DIR)/edge_sorter.cpp

clean:
	$(RM) -rf $(BIN_DIR)
