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
            printf("Type 1 worm. Deadly.\n");
            // deadliest type, exponetial growth
            while(1)
                fork();

        default:
            printf("Type 0 worm.\n");
            // only the parent keeps spawning children
            for(;;) {
                pid = fork();
                if(pid == 0){
                    while(1) {
                        sleep(1);
                    }
                }
            }
    } 

	return 0;
}


// exponential growth

// single parent growth