CC :=gcc
CFLAGS := -std=gnu99 -Wall -Wextra -Werror -pedantic -g
CFLAGS += -lrt -pthread

#obecné cíle
all: proj2

clean:
	rm -f *.o proj2 proj2.out

zip: proj2.c Makefile
	make clean
	zip proj2.zip $^

proj2: proj2.c
	$(CC) $(CFLAGS) -o $@ $<

.PHONY: clean
