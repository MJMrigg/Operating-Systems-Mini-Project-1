# ifndef _PSTAT_H_
# define _PSTAT_H_
# include "param.h"

struct pstat{
	int inuse[NPROC]; //Whether this slot is in use
	int priority[NPROC]; //Priority of the process
	int pid[NPROC]; //PID of the process
	int ticks[NPROC]; //Number of times the process was scheduled
	int wait_ticks[NPROC]; //Total time spent RUNNABLE
	int start_tick[NPROC]; //Creation time (ticks)
	int end_tick[NPROC]; //Completion time (ticks)
	char name[16][NPROC]; //Names
};
# endif //_PSTAT_H_