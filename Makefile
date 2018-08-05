#
# Makefile
# Peter Jones, 2018-08-05 14:33
#

TARGETS = hc16dis
CC = gcc
CFLAGS = \
	 -Wall -Wextra \
	 -Wno-missing-field-initializer \
	 -Werror

all: $(TARGETS)

% : %.o
	$(CC) $(CFLAGS) -o $@ $^

%.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $^


clean :
	@rm -vf hc16dis *.o *.a *.so

.PHONY : clean all

# vim:ft=make
#
