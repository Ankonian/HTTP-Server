GCC = gcc -Wall -Wextra -Wpedantic -pthread -g
EXE = httpserver

${EXE}: httpserver.c
	${GCC} -o $@ $^

clean:
	rm -f ${EXE} *.o