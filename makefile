CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -lSDL3 

SRC = main.c 
OBJ = $(SRC:.c=.o)

EXEC = Ball

# Default target
all: $(EXEC)

# Link the object files to create the executable
$(EXEC): $(OBJ)
	$(CC) $(OBJ) -o $(EXEC) $(LDFLAGS)

# Compile the source files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up build files
clean:
	rm -f $(OBJ) $(EXEC)