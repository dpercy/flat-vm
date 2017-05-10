
flags= -Wall -O3 -mdynamic-no-pic

default: vm

main.s: main.c
	gcc main.c $(flags) -S -o main.s

vm: main.s
	gcc main.s -o vm

clean:
	rm -f main.s vm
