proj2: proj2.o
	gcc proj2.c -std=gnu99 -Wall -Wextra -lpthread -Werror -pedantic -o proj2

proj2.o: proj2.c
	gcc -c proj2.c -std=gnu99 -Wall -Wextra -lpthread -Werror -pedantic

clean:
	rm *.o proj2