CC := clang

TARGET := main
SPIRES_DIR := ../spires

SOURCES := main.c spires_interface.c crossbar_generator.c read_crossbar.c
OBJECTS := $(SOURCES:.c=.a)

PLPLOT_LIBS   := $(shell pkg-config --libs plplot)

CPPFLAGS := -I. -I$(SPIRES_DIR)/include
CFLAGS := -Wall -Wextra -Wpedantic -std=c11 -g -fopenmp
LDFLAGS := -L$(SPIRES_DIR)/lib -Wl,-rpath,'$$ORIGIN/../spires/lib'
LDLIBS := -lspires -lopenblas -llapacke -lm -fopenmp $(PLPLOT_LIBS)

PLPLOT_CFLAGS := $(shell pkg-config --cflags plplot)
CFLAGS += $(PLPLOT_CFLAGS)

.PHONY: all run clean debug

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $(TARGET)

%.a: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	LD_LIBRARY_PATH=$(SPIRES_DIR)/lib:$$LD_LIBRARY_PATH ./$(TARGET)

debug: CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
debug: LDFLAGS += -fsanitize=address,undefined
debug: clean $(TARGET)

clean:
	rm -f $(OBJECTS) $(TARGET)
