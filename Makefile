all:
	gcc -Wall -Wextra -Werror -g -o sshell sshell.c

clean:
	rm -f sshell
