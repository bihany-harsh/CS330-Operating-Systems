#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <math.h>

int main(int argc, char* argv[])
{
	if (argc < 2) {
		printf("Unable to execute\n");
		exit(-1);
	}
	
	if (argc == 2){
		int n = atoi(argv[argc - 1]);
		double n_d = (double)n;
		n = (int)sqrt(n_d + 0.5);
		printf("%d\n", n);
		return 0;
	}

	int pid;

	pid = fork();

	if (pid < 0) {
		// perror("fork");
		printf("Unable to execute\n");
		exit(-1);
	}

	if (!pid) { // child
		int n = atoi(argv[argc - 1]);
		double n_d = (double)n;
		n = (int)sqrt(n_d + 0.5);

		sprintf(argv[argc - 1], "%d", n);

		char new_exc[9];
		strcpy(new_exc, "./");
		strcat(new_exc, argv[1]);
		new_exc[8] = '\0';

		char* new_argv[argc];

		for (int i = 1; i < argc; i++) {
			if (i == 1) {
				new_argv[i - 1] = new_exc;
			}
			else {
				new_argv[i - 1] = argv[i];
			}
		}

		new_argv[argc - 1] = NULL;
		
		if (execvp(new_exc, new_argv)) {
			// perror("exec");
			printf("Unable to execute\n");
		}
		exit(-1);

	} else {
		wait(NULL);
	}

	return 0;
}

