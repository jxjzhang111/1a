#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/resource.h>

int main(int argc, char** argv) {
	int pid, i, type = 0;
    if(argc > 1) {
        type = atoi(argv[1]);
    }

    switch(type) {
        case 1:
            printf("Type I. Deadly.\n");
            // deadliest type, exponetial growth
            while(1)
                fork();

        case 2:
            printf("Type II.\n");
            // only the parent keeps spawning children
            for(;;) {
                pid = fork();
                if(pid == 0){
                    while(1) {
                        sleep(1);
                    }
                }
            }

        default:
            printf("Type III.\n");
            // 70% chance of forking a child
            while(rand() % 100 < 70)
                fork();
    } 

	return 0;
}
