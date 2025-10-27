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
    create_queue(&queues[i]);
  }
}

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  create_queues(); //Create the queues
}

void
getprocinfo(int pid)
{
  //Get process table
  acquire(&ptable.lock);
  struct proc *p;
  //Go through all of the processes
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    //If this was the process we were looking for, print its wait ticks in all the queues
    if(p->pid == pid){
      for(int i = 0, j = 3; j >= 0; i++, j--){
        cprintf("Level %d: ticks-used: %d\n", j, p->ticks[i]);
      }
      //Stop going through the process table
      break;
    }
  }
  release(&ptable.lock);
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
  //The process has no ticks or wait ticks
  for(int i = 0; i < 4; i++){
    p->ticks[i] = 0;
    p->wait_ticks[i] = 0;
  }

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

  //enqueue(&queues[0],p);

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

  //Add process to the highest priority queue
  enqueue(&queues[0],p);

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
  //Add process to the highest priority queue
  enqueue(&queues[0],np);
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
int rr_slices[4] = {1,2,4,64};
int time_slices = 8;

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
      swtch(&cpu->scheduler, p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      //proc = 0;
    }
    release(&ptable.lock);*/
    
    acquire(&ptable.lock);
    //Go through each queue and get the highest priority runnable process
    for(int i = 0; i < 4; i++){
      int j = 0;
      while(j < NPROC){
        //If the current process still has round robin slices and it can be run, run it
        if(proc != NULL && proc->rr_slice_left > 0 && proc->state == RUNNABLE){
          goto switch_process;
        }
        //If there is no process in that index of the queue, move on
        if(queues[i].procs[j] == NULL){
          j += 1;
          continue;
        }
        //If the process in that index of the queue is not runnable, move on
        if(queues[i].procs[j]->state != RUNNABLE){
          j += 1;
          continue;
        }
        //If the process in that index of the queue has run out of time slices, move it to the next lower priority
        if(queues[i].procs[j]->timeslice_left <= 0 && queues[i].procs[j]->priority > 0){
          struct proc *temp = queues[i].procs[j];
          dequeue(&queues[i],queues[i].procs[j]->pid);
          enqueue(&queues[i+1],temp);
          //Reset time slices and rr slices
          temp->timeslice_left = time_slices;
          temp->rr_slice_left = rr_slices[i+1];
          //Assign new priority
          temp->priority -= 1;
          //Go back to the beginning of the queue
          j = 0;
          continue;
        }
        //If the process in that index of the queue has run out of rr slices, move it to the back of the queue
        if(queues[i].procs[j]->rr_slice_left <= 0 && queues[i].procs[j]->priority > 0){
          struct proc *temp = queues[i].procs[j];
          dequeue(&queues[i],queues[i].procs[j]->pid);
          enqueue(&queues[i],temp);
          //Reset rr slices
          temp->rr_slice_left = rr_slices[i];
          //Go back to the beginning of the queue
          j = 0;
          continue;
        }
        //If a process made it this far, it means it's a runnable process that isn't out of rr slots or time slots
        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        proc = queues[i].procs[j];

        switch_process:
        switchuvm(proc);
        proc->state = RUNNING;
        swtch(&cpu->scheduler, proc->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.

        //Update wait ticks of the processes
        struct proc *p;
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
          //If p is the current process, don't increase its wait ticks, as it was running
          if(p == proc){
            continue;
          }
          //Get queue of the process and then increase its wait ticks on that queue
          int queue = (p->priority - 3) * -1;
          p->wait_ticks[queue] += 1;
          //If the process has been waiting too long, boost it up a level
          if(p->state == RUNNABLE && p->priority < 3 && (p->wait_ticks[queue] % (rr_slices[queue]*time_slices*10)) == 0){
            dequeue(&queues[queue],p->pid);
            enqueue(&queues[queue-1],p);
            p->rr_slice_left = rr_slices[queue-1];
            p->timeslice_left = time_slices;
            p->priority += 1;
          }
        }
        //Go back to the start of the queue so that lower priority processes aren't scheduled over higher ones
        //j = 0; 
      }
    }
    release(&ptable.lock);
    switchkvm();
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


