#include "types.h"
#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "queue.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

// Queues
struct queue queues[4];
//Create the queues
void
create_queues(void){
  for(int i = 0; i < 4; i++){
    create_queue(queues[i]);
  }
}

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  create_queues(); //Create the queues
}

void
ps(void)
{
  struct proc *p; //Pointer to point at processes
  //Print table headers
  cprintf("PID\tState\t\tName\tSize\tParent\tPriority\tRR Slices\tTime Slice\n");
  cprintf("---\t-----\t\t----\t----\t------\t--------\t---------\t----------\n");
  //Get the process table
  acquire(&ptable.lock);
  //Go through each process in the process table
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    //If this process is unused, don't print it. It's done
    if(p->state == UNUSED){
      continue;
    }
    //Print the PID, state, name, size, and parent of the process
    cprintf("%d\t", p->pid);
    if(p->state == SLEEPING){
      cprintf("Sleeping\t\t");
    }else if(p->state == RUNNABLE){
      cprintf("Runnable\t\t");
    }else if(p->state == RUNNING){
      cprintf("Running\t\t");
    }else if(p->state == ZOMBIE){
      cprintf("Zombie\t\t");
    }else{
      cprintf("Unknown\t\t");
    }
    cprintf("%s\t%d\t", p->name, p->sz);
    if(p->parent){
      cprintf("%d\t", p->parent);
    }else{
      cprintf("No Parent\t");
    }
    cprintf("%d\t%d\t%d\n",p->priority, p->rr_slice_left, p->timeslice_left);
  }
  //Release the process table
  release(&ptable.lock);
}

// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      goto found;
  }
  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->priority = 3; //Set priority to highest queue
  p->timeslice_left = 8; //The process has 8 time slots left in the current time slice
  p->rr_slice_left = 1; //The process has 1 time slot left in the current round robin time slice
  p->next = NULL; //There is no process next in the queue

  release(&ptable.lock);

  // Allocate kernel stack if possible.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;
  
  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;
  
  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  //Add process to the highest priority queue
  enqueue(&queues[0],p);

  return p;
}

// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];
  
  p = allocproc();
  acquire(&ptable.lock);
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  
  sz = proc->sz;
  if(n > 0){
    if((sz = allocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(proc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  proc->sz = sz;
  switchuvm(proc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;

  // Allocate process.
  if((np = allocproc()) == 0)
    return -1;

  // Copy process state from p.
  if((np->pgdir = copyuvm(proc->pgdir, proc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = proc->sz;
  np->parent = proc;
  *np->tf = *proc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(proc->ofile[i])
      np->ofile[i] = filedup(proc->ofile[i]);
  np->cwd = idup(proc->cwd);
 
  pid = np->pid;
  np->state = RUNNABLE;
  safestrcpy(np->name, proc->name, sizeof(proc->name));
  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *p;
  int fd;

  if(proc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(proc->ofile[fd]){
      fileclose(proc->ofile[fd]);
      proc->ofile[fd] = 0;
    }
  }

  iput(proc->cwd);
  proc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(proc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == proc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  proc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for zombie children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != proc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->state = UNUSED;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || proc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(proc, &ptable.lock);  //DOC: wait-sleep
  }
}

//RR and time slices for each queue
int slices[4][4] = {
  {1,2,4,64},     //RR time slices
  {8,16,32,64}    //Time slices
};

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;

  for(;;){
    // Enable interrupts on this processor.
    sti();

    /*//OLD SCHEDULER
    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      proc = p;
      switchuvm(p);
      p->state = RUNNING;
      cprintf(" %d%d ",p->pid, proc->pid);
      swtch(&cpu->scheduler, p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      //proc = 0;
    }
    release(&ptable.lock);*/
    
    acquire(&ptable.lock);
    //Go through each queue and look for the highest priority runnable process
    for(int i = 0; i < 4; i++){
      p = queues[i].head; //Start at the beginning

      //Go through each process in the queue
      for(int j = 0; j < queues[i].size; j++){
        //If the process isn't runable, move on
        if(p->state != RUNNABLE){
          p = p->next;
          continue;
        }
        //If the process is out of time slices, move it to a lower priority queue
        if(p->timeslice_left <= 0 && p->priority > 0){
          struct proc *temp = p; //Get process
          p = p->next; //Set p to the next process in the queue
          //Remove the process from the queue and send it to the lower queue
          dequeue(&queues[i],temp->pid);
          enqueue(&queues[i+1],temp);
          //Set up rr slices, time slices, and priority
          temp->rr_slice_left = slices[0][i+1];
          temp->timeslice_left = slices[1][i+1];
          temp->priority -= 1;
          continue;
        }
        //If the process is out of round robin slices, move it to the back of the queue but keep its priority
        if(p->rr_slice_left <= 0){
          struct proc *temp = p; //Get process
          p = p->next; //Set p to next process in the queue
          //Remove process from the front of the queue and send it to the back of the same queue
          dequeue(&queues[i],temp->pid);
          enqueue(&queues[i],temp);
          //Reset rr slices
          temp->rr_slice_left = slices[0][i];
          continue;
        }
        //If the process made it this far, it is a runnable process not out of time or rr slices
        
        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        proc = p;
        switchuvm(p);
        p->state = RUNNING;
        swtch(&cpu->scheduler, proc->context);
        switchkvm();
        p->ticks[i] += 1; //Increase tick counter

        // Process is done running for now.
        // It should have changed its p->state before coming back.

        //Move on to the next process
        proc = 0;
        goto break_twice; //The process was found. Stop going through the queues
      }
    }
    break_twice:
    release(&ptable.lock);

    int chosen_process_pid = p->pid; //Keep track of chosen process

    //Increase wait times of all processes by going through each process in each queue
    for(int i = 0; i < 4; i++){
      p = queues[i].head;
      for(int j = 0; j < queues[i].size; j++){
        //If the current process was the run, don't increase its wait time. It ran, it didn't wait
        if(p->pid = chosen_process_pid){
          p = p->next;
          continue;
        }
        //Increase wait time in the queue for the process and then move on to the next one
        p->wait_ticks[i] += 1;
        p = p->next;
      }
    }

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state.
void
sched(void)
{
  int intena;

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(cpu->ncli != 1)
    panic("sched locks");
  if(proc->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = cpu->intena;
  swtch(&proc->context, cpu->scheduler);
  cpu->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  proc->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);
  
  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  if(proc == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }

  // Go to sleep.
  proc->chan = chan;
  proc->state = SLEEPING;
  sched();

  // Tidy up.
  proc->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];
  
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}


