GCC=gcc
FLAGS=-Wall -O2
LIBS=
INCLUDES=
OBJ=main.o

main.o : src/main.c
	${GCC} ${FLAGS} -c src/main.c ${INCLUDES}

all : ${OBJ}
	${GCC} ${FLAGS} ${OBJ} -o quiiStream.out ${LIBS}

clean :
	rm *.o *.out
