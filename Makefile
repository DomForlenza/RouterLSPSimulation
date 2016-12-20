CC = g++
CFLAGS = -std=c++11 -fopenmp -pedantic -Wall -Wextra -c
LFLAGS = -std=c++11 -fopenmp -pedantic -Wall -Wextra -o
SRC = manager.cpp
OBJ = $(SRC:.cpp=.o)
EXE = manager

.PHONY: clean

all: clean $(EXE)
	@echo Executable $(EXE) has been created.

$(EXE): $(OBJ)
	@echo Creating $(EXE) executable...
	@$(CC) $(LFLAGS) $(EXE) $(OBJ)
	@$(RM) *.o ~*

.cpp.o:
	@echo Compiling $< into an object file...
	@$(CC) $(CFLAGS) $< 

clean:
	@echo Cleaning up...
	@$(RM) *.o ~* $(EXE)
	@echo All object and executables removed.
