#ifndef __GT_UTHREAD_H
#define __GT_UTHREAD_H
#include<sys/time.h>
/* User-level thread implementation (using alternate signal stacks) */

typedef unsigned int uthread_t;
typedef unsigned int uthread_group_t;

/* uthread states */
#define UTHREAD_INIT 0x01
#define UTHREAD_RUNNABLE 0x02
#define UTHREAD_RUNNING 0x04
#define UTHREAD_CANCELLED 0x08
#define UTHREAD_DONE 0x10
//#define NUM_THREADS 128

/* uthread struct : has all the uthread context info */
typedef struct uthread_struct
{
	int uthread_state; /* UTHREAD_INIT, UTHREAD_RUNNABLE, UTHREAD_RUNNING, UTHREAD_CANCELLED, UTHREAD_DONE */
	int uthread_priority; /* uthread running priority */
	int uthread_credit;
	int uthread_initial_credit;
	int cpu_id; /* cpu it is currently executing on */
	int last_cpu_id; /* last cpu it was executing on */
	
	uthread_t uthread_tid; /* thread id */
	uthread_group_t uthread_gid; /* thread group id  */
	int (*uthread_func)(void*);
	void *uthread_arg;

	int elapsed_time;
//	struct timeval *start_time;
	int group_id;
	int start_time_sec;
	int start_time_usec;
	int finish_time_sec;
	int finish_time_usec;

	void *exit_status; /* exit status */
	int reserved1;
	int reserved2;
	int reserved3;
	
	sigjmp_buf uthread_env; /* 156 bytes : save user-level thread context*/
	stack_t uthread_stack; /* 12 bytes : user-level thread stack */
	TAILQ_ENTRY(uthread_struct) uthread_runq;
} uthread_struct_t;

typedef struct __uthread_shared_info
{
	int thread_id;
	int group_id;
	int elapsed_time;
	int uthread_credit;
	int initial_credit;
	int matrix_size;
	int total_run_time;
	int finish_time_sec;
	int finish_time_usec;
	int start_time_sec;
	int start_time_usec;
} uthread_shared_info_t;

/*typedef struct __group_stats
{
	int group_id;
	int execution_time;
	int total_run_time;
	int credit_value;
	int matrix_size;
	int mean;
	int standard_deviation;
} group_stats_t;
*/
struct __kthread_runqueue;
uthread_shared_info_t *uthread_shared_info[128];
extern void uthread_schedule(uthread_struct_t * (*kthread_best_sched_uthread)(struct __kthread_runqueue *));
#endif
