# Compiler to use
CC = gcc

# Compiler flags
CFLAGS = -Wall

# Target executables
TARGETS = call internal safety

# Source files
CALL_SRC = call.c
INTERNAL_SRC = internal.c
SAFETY_SRC = safety.c

# Object files
CALL_OBJ = $(CALL_SRC:.c=.o)
INTERNAL_OBJ = $(INTERNAL_SRC:.c=.o)
SAFETY_OBJ = $(SAFETY_SRC:.c=.o)

# Default rule to build all targets
all: $(TARGETS)

# Rule to build call executable
call: $(CALL_OBJ)
	$(CC) $(CFLAGS) -o call $(CALL_OBJ)

# Rule to build internal executable
internal: $(INTERNAL_OBJ)
	$(CC) $(CFLAGS) -o internal $(INTERNAL_OBJ)

# Rule to build safety executable
safety: $(SAFETY_OBJ)
	$(CC) $(CFLAGS) -o safety $(SAFETY_OBJ)

# Rule to compile .c files to .o files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean rule to remove object and executables
clean:
	rm -f $(CALL_OBJ) $(INTERNAL_OBJ) $(SAFETY_OBJ) $(TARGETS)
