#include <stdio.h>

char buffer[100];
int i;

main()
{
	while (fgets(buffer,100,stdin) != NULL) {
		for (i=0; i < strlen(buffer); i++) {
			if (buffer[i] == ' ') {
				putchar('\t');
				while (buffer[i] == ' ') i++;
			}
			putchar(buffer[i]);
		}
	}
}