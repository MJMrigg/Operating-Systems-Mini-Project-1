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

/*
//Queue to hold the processes
struct queue{
    struct proc *head; //Start of the queue
    struct proc *tail; //End of the queue
    int size;          //Number of processes in the queue
};

void create_queue(struct queue *q){
    //There are no processes in the queue initially
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
}

void enqueue(struct queue *q, struct proc *new){
    //If this was the first process, have the head point to the new process
    if(q->head == NULL || q->size <= 0){
        q->head = new;
    }else{
    //If it wasn't, have the process at the end of the queue point to the new process
        q->tail->next = new;
    }
    //Have the tail point to the new process
    q->tail = new;
    //Increase the size by 1
    q->size += 1;
}

void dequeue(struct queue *q, int pid){
    //If the queue is empty, do nothing
    if(q->head == NULL || q->size <= 0){
        return;
    }
    //If the process is at the head of the queue, simply have the head stop pointing at the process
    if(q->head->pid == pid){
        struct proc *temp = q->head;
        q->head = q->head->next;
        temp->next = NULL;
        q->size -= 1;
        return;
    }
    //If it was not, start looking through the whole queue
    struct proc *cur = q->head->next; //Start at the second process, as was already know the head is not the one we're looking for
    struct proc *parent = q->head; //Keep track of the process that came before it in the queue(will be useful)
    while(cur != NULL){
        //If the process was found, remove it from the queue by making the one before point to the one after it
        if(cur->pid == pid){
            parent->next = cur->next;
            cur->next = NULL;
            q->size -= 1;
            return;
        }
        //If it wasn't, move on to the next process
        cur = cur->next;
        parent = parent->next;
    }

}*/
# endif