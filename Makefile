DIR_INC = ./
DIR_SRC = ./
DIR_OBJ = ./obj
DIR_BIN = ./bin

SRC = $(wildcard ${DIR_SRC}/*.cpp)
OBJ = $(patsubst %.cpp,${DIR_OBJ}/%.o,$(notdir ${SRC}))

TARGET = main

BIN_TARGET = ${DIR_BIN}/${TARGET}

CC = g++
CFLAGS = -std=c++11 -O2 -g -Wall -I${DIR_INC}

${BIN_TARGET}:${OBJ}
	$(CC) $(OBJ) -o $@

${DIR_OBJ}/%.o:${DIR_SRC}/%.cpp
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY:clean
    
clean:
#	find ${DIR_OBJ} -name "*.o" -exec rm -rf{}
	find ${DIR_OBJ} -name "*.o" | xargs rm -rf
	rm -rf ${BIN_TARGET}
    

