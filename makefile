CC = clang

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
BIN_DIR = bin

TARGET = $(BIN_DIR)/benchmark

SOURCES = \
	$(SRC_DIR)/application.c \
	$(SRC_DIR)/spires_interface.c \
	$(SRC_DIR)/crossbar_generator.c \
	$(SRC_DIR)/read_crossbar.c \
	$(SRC_DIR)/benchmark.c 

OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

CPPFLAGS = \
	-I$(INC_DIR) \
	-Ispires/include \
	-I/usr/include/plplot

CFLAGS = \
	-Wall \
	-Wextra \
	-Wpedantic \
	-std=c11 \
	-g \
	-fopenmp

LDFLAGS = \
	-Lspires/lib \
	-Wl,-rpath,'$$ORIGIN/../spires/lib'

LDLIBS = \
	-lspires \
	-lopenblas \
	-llapacke \
	-lplplot \
	-lm \
	-fopenmp

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(CC) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
