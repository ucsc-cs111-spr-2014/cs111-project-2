#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

int main(int argc, char *argv[]){
    printf("argv=%d\n", atoi(argv[1]));
    
    int x = 3;
    clock_t start, end, total;
    start = clock();
    int sec = atoi(argv[1]);

    while(1){
        x = (x * x) / 2;
        end = clock();
        total =    (double)(end - start) /	CLOCKS_PER_SEC;
        if(total == sec){
            printf("%s:%d %s:%d\n", "Total", total, "Seconds", sec);
            printf("%s\n", "Done.");
            return(0);
        }
    }
}
