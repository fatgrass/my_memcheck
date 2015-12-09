###############################################################################

## COMPILER ##
CXX       = g++

## FLAGS ##
CXXFLAGS  = -Wall -Wextra -Werror -pedantic  -std=c++11  -I $(INCLDIR)
CXXFLAGS += -pedantic -g
#CXXFLAGS += -Wundef -Wshadow -Wpointer-arith -Wcast-qual
#CXXFLAGS += -Wcast-align
#CXXFLAGS += -Wmissing-declarations
#CXXFLAGS += -Wunreachable-code
CXXFLAGS += -fdiagnostics-color=always

## INCLUDES DIRECTORY ##
INCLDIR   = src/includes/

## MAIN ##
SRCS     = $(addsuffix .cc, $(addprefix src/, main))
SRCS    += $(addsuffix .cc, $(addprefix src/level1/, strace syscalls))
SRCS    += $(addsuffix .cc, $(addprefix src/helpers/, helpers))

## OBJ CREATION ##
OBJS      = $(SRCS:.cc=.o)

## EXEC NAME ##
EXEC      = memcheck

###############################################################################

# Produce the final binary   #
all: $(OBJS)
	$(CXX) $(OBJS) $(LDLIBS) -o $(EXEC)

# Multi threaded make of the final binary #
multi:
	$(MAKE) -Bj all

# Produce test binary, and launch #
check: multi
	./$(EXEC) ./hardcoded

# Clean repository           #
clean:
	$(RM) $(OBJS) $(EXEC)

.PHONY: multi all check clean bonus

###############################################################################
