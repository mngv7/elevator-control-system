# Compiler to use
CC = gcc

# Compiler flags
CFLAGS = -Wall -I.  # Add -I. to include the current directory for header files

# Target executables
TARGETS = call internal safety controller car

# Source files
CALL_SRC = call.c
INTERNAL_SRC = internal.c
SAFETY_SRC = safety.c
CONTROLLER_SRC = controller.c
NETWORK_UTILS_SRC = network_utils.c
CAR_SRC = car.c

# Object files
CALL_OBJ = $(CALL_SRC:.c=.o)
INTERNAL_OBJ = $(INTERNAL_SRC:.c=.o)
SAFETY_OBJ = $(SAFETY_SRC:.c=.o)
CONTROLLER_OBJ = $(CONTROLLER_SRC:.c=.o)
NETWORK_UTILS_OBJ = $(NETWORK_UTILS_SRC:.c=.o)
CAR_OBJ = $(CAR_SRC:.c=.o)

# Default rule to build all targets
all: $(TARGETS)

# Rule to build call executable
call: $(CALL_OBJ) $(NETWORK_UTILS_OBJ)  # Link against network_utils.o
	$(CC) $(CFLAGS) -o call $(CALL_OBJ) $(NETWORK_UTILS_OBJ)

# Rule to build internal executable
internal: $(INTERNAL_OBJ)
	$(CC) $(CFLAGS) -o internal $(INTERNAL_OBJ)

# Rule to build safety executable
safety: $(SAFETY_OBJ)
	$(CC) $(CFLAGS) -o safety $(SAFETY_OBJ)

# Rule to build controller executable
controller: $(CONTROLLER_OBJ) $(NETWORK_UTILS_OBJ)
	$(CC) $(CFLAGS) -o controller $(CONTROLLER_OBJ) $(NETWORK_UTILS_OBJ)

# Rule to build car executable
car: $(CAR_OBJ) $(NETWORK_UTILS_OBJ)
	$(CC) $(CFLAGS) -o car $(CAR_OBJ) $(NETWORK_UTILS_OBJ)

# Rule to compile .c files to .o files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean rule to remove object files and executables
clean:
	rm -f $(CALL_OBJ) $(INTERNAL_OBJ) $(SAFETY_OBJ) $(CONTROLLER_OBJ) $(NETWORK_UTILS_OBJ) $(CAR_OBJ) $(TARGETS)
