# include "queue.h"

void create_queue(struct queue *q){
    //There are no processes in the queue initially
    q->head = NULL;
    q->tail = NULL;
    q->size = 0;
}

void enqueue(struct queue *q, struct proc *new){
    //Have the process at the end of the queue point to the new process
    q->tail->next = new;
    //Have the tail point to the new process
    q->tail = new;
    //If this was the first process, have the head point to the new process
    if(q->head == NULL || q->size == 0){
        q->head = new;
    }
    //Increase the size by 1
    q->size += 1;
}

void dequeue(struct queue *q){
    //If the queue is empty, do nothing
    if(q->head == NULL || q->size == 0){
        return;
    }
    //Have the head point to the next process
    struct proc *temp = q->head;
    q->head = q->head->next;
    //Have the previous head leave the queue by no longer pointing to the next process in the queue
    temp->next = NULL;
    //Decrease the size by 1
    q->size -= 1;
}