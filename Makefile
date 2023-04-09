CC=gcc

shell: boone.o editor.o
	$(CC) -o a editor.o boone.o
	rm -f *.o
