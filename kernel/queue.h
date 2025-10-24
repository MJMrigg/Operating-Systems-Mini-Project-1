# ifndef _QUEUE_H_
# define _QUEUE_H_
# include "proc.h"
# include "types.h"

struct queue{
    struct proc *procs[NPROC];
};

void create_queue(struct queue *q){
    //There are no processes in the queue initially
    for(int i = 0; i < NPROC; i++){
        q->procs[i] = NULL;
    }
}

void enqueue(struct queue *q, struct proc *new){
    //Go through the queue and look for a null pointer to point to the new process
    for(int i = 0; i < NPROC; i++){
        //Once the null pointer has been found, point it to the new process and then return
        if(q->procs[i] != NULL){
            continue;
        }
        q->procs[i] = new;
        return;
    }
}

void dequeue(struct queue *q, int pid){
    //Go through the queue and look for the process with the pid
    for(int i = 0; i < NPROC; i++){
        //Once the process has been found, take it out of the queue by having its pointer point to null
        if(q->procs[i]->pid != pid){
            continue;
        }
        q->procs[i] = NULL;
        return;
    }
}

# endif // _QUEUE_H_