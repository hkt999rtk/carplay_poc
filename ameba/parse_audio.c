#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
	if (argc<2) {
		printf("usage: %s [file]\n", argv[0]);
		exit(1);
	}

	FILE *infile = fopen(argv[1], "rt");
	if (!infile) {
		printf("error: cannot open %s\n", argv[1]);
		exit(1);
	}
	char line[256];

	int i=0;
	while (!feof(infile)) {
		char *s = fgets(line, sizeof(line), infile);
		s[strlen(s)-1] = 0;

		printf("\"%s\",", s);
		if (++i%8==0)
			printf("\n");
	}

	return 0;
}

