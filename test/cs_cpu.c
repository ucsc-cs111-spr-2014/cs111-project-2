#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

int main(int argc, char *argv[]){    
    int x, sec;
    clock_t start, end, total;
    start = clock();
    sec = atoi(argv[1]);
    x = 3;

    while(1){
        x = (x * x) / 2;
        end = clock();
        total = (double)(end - start) /	CLOCKS_PER_SEC;
        if(total == sec){
            printf("%s:%d %s:%d\n", "Total", total, "Seconds", sec);
            printf("%s\n", "Done.");
            return(0);
        }
    }
}
