# ifndef _QUEUE_H_
# define _QUEUE_H_
# include "proc.h"
# include "types.h"


//Queue to hold the processes
struct queue{
    struct proc *head; //Start of the queue
    struct proc *tail; //End of the queue
    int size;          //Number of processes in the queue
};

void create_queue(struct queue *q);
void enque(struct queue *q, struct proc *new);
void dequeue(struct queue *q);

# endif