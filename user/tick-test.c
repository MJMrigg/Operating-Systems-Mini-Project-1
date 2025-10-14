#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[]){
    //Create a pstat table
    if(argc != 2){
        printf(1, "usage: mytest counter");
        exit();
    }
    //Wait an amount of ticks
    int x;
    int mypid = (int)getpid();
    for(int i = 1; i < atoi(argv[1]); i++){
        x = x + i;
    }
    //Print the amount of ticks this process was waiting
    getprocinfo(mypid);
    //Exit
    exit();
}