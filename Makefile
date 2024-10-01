# Compiler to use
CC = gcc

# Compiler flags
CFLAGS = -Wall

# Target executables
TARGETS = call internal

# Source files
CALL_SRC = call.c
INTERNAL_SRC = internal.c

# Object files
CALL_OBJ = $(CALL_SRC:.c=.o)
INTERNAL_OBJ = $(INTERNAL_SRC:.c=.o)

# Default rule to build all targets
all: $(TARGETS)

# Rule to build call executable
call: $(CALL_OBJ)
	$(CC) $(CFLAGS) -o call $(CALL_OBJ)

# Rule to build internal executable
internal: $(INTERNAL_OBJ)
	$(CC) $(CFLAGS) -o internal $(INTERNAL_OBJ)

# Rule to compile call.c to call.o
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean rule to remove object and executables
clean:
	rm -f $(CALL_OBJ) $(INTERNAL_OBJ) $(TARGETS)
