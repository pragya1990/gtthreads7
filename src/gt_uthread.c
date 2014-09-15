#include <stdio.h>
#include <unistd.h>
#include <linux/unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sched.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <assert.h>
#include <time.h>
//#include <ctime>
#include "gt_include.h"
/**********************************************************************/
/** DECLARATIONS **/
/**********************************************************************/


/**********************************************************************/
/* kthread runqueue and env */

/* XXX: should be the apic-id */
#define KTHREAD_CUR_ID	0
//#define NUM_THREADS 128

/**********************************************************************/
/* uthread scheduling */
static void uthread_context_func(int);
static int uthread_init(uthread_struct_t *u_new);
// uthread shared info
//static uthread_shared_info_t *uthread_shared_info[NUM_THREADS];
void update_uthread_shared_statistics(uthread_struct_t *u_obj);
extern void print_statistics();
extern void gt_yield();
/**********************************************************************/
/* uthread creation */
#define UTHREAD_DEFAULT_SSIZE (16 * 1024)

extern int uthread_create(uthread_t *u_tid, int (*u_func)(void *), void *u_arg, uthread_group_t u_gid, int credit);
extern void uthread_info_init();
/**********************************************************************/
/** DEFNITIONS **/
/**********************************************************************/

/**********************************************************************/
/* uthread scheduling */

/* Assumes that the caller has disabled vtalrm and sigusr1 signals */
/* uthread_init will be using */
static int uthread_init(uthread_struct_t *u_new)
{
//	printf("Inside uthread_init\n");
	stack_t oldstack;
	sigset_t set, oldset;
	struct sigaction act, oldact;

	gt_spin_lock(&(ksched_shared_info.uthread_init_lock));

	/* Register a signal(SIGUSR2) for alternate stack */
	act.sa_handler = uthread_context_func;
	act.sa_flags = (SA_ONSTACK | SA_RESTART);
	if(sigaction(SIGUSR2,&act,&oldact))
	{
		fprintf(stderr, "uthread sigusr2 install failed !i!\n");
		return -1;
	}

	/* Install alternate signal stack (for SIGUSR2) */
	if(sigaltstack(&(u_new->uthread_stack), &oldstack))
	{
		fprintf(stderr, "uthread sigaltstack install failed.\n");
		return -1;
	}

	/* Unblock the signal(SIGUSR2) */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	sigprocmask(SIG_UNBLOCK, &set, &oldset);


	/* SIGUSR2 handler expects kthread_runq->cur_uthread
	 * to point to the newly created thread. We will temporarily
	 * change cur_uthread, before entering the synchronous call
	 * to SIGUSR2. */

	/* kthread_runq is made to point to this new thread
	 * in the caller. Raise the signal(SIGUSR2) synchronously */
#if 0
	raise(SIGUSR2);
#endif
	syscall(__NR_tkill, kthread_cpu_map[kthread_apic_id()]->tid, SIGUSR2);

	/* Block the signal(SIGUSR2) */
	sigemptyset(&set);
	sigaddset(&set, SIGUSR2);
	sigprocmask(SIG_BLOCK, &set, &oldset);
	if(sigaction(SIGUSR2,&oldact,NULL))
	{
		fprintf(stderr, "uthread sigusr2 revert failed !!");
		return -1;
	}

	/* Disable the stack for signal(SIGUSR2) handling */
	u_new->uthread_stack.ss_flags = SS_DISABLE;

	/* Restore the old stack/signal handling */
	if(sigaltstack(&oldstack, NULL))
	{
		fprintf(stderr, "uthread sigaltstack revert failed.");
		return -1;
	}

	gt_spin_unlock(&(ksched_shared_info.uthread_init_lock));
	return 0;
}

/*
extern void print_statistics() 
{
	printf("**************** Printing statistics ************\n");
	int i;
	int start_time_sec;
	int start_time_usec;
	int finish_time_sec;
	int finish_time_usec; 
	for(i=0;i<NUM_THREADS;i++)
	{
		start_time_sec = uthread_shared_info[i]->start_time_sec;
		start_time_usec = uthread_shared_info[i]->start_time_usec;
		finish_time_sec = uthread_shared_info[i]->finish_time_sec;
		finish_time_usec = uthread_shared_info[i]->finish_time_usec;

		if(finish_time_sec < start_time_sec)
		printf("Error calculating time finish_time less than start_time**************\n");
		int sec = (finish_time_sec - start_time_sec);
		int usec = 0;
		if(finish_time_usec >= start_time_usec) 
			usec = finish_time_usec - start_time_usec;
		else
		{	if(sec <=0)
				printf("Error: Seconds cant be less than 0******************\n");
			sec = sec-1;
			usec = (1000000 - start_time_usec) + finish_time_usec;
		}
		int run_time = sec*1000000 + usec;
		uthread_shared_info[i]->total_run_time =run_time;
		
		printf("kthread id: %d, group_id: %d,total_run_time: %d, execution_time: %d, credits: %d, initial_credits: %d\n",uthread_shared_info[i]->thread_id,
			uthread_shared_info[i]->group_id, uthread_shared_info[i]->total_run_time, uthread_shared_info[i]->elapsed_time,
			uthread_shared_info[i]->uthread_credit, uthread_shared_info[i]->initial_credit);
	}
	return;
}
*/

/* Initialize the uthread_shared_info */
extern void uthread_info_init()
{
//	printf("Inside uthread_info_init\n");
	int i;
	for(i=0;i<NUM_THREADS;i++)
	{
		//printf("INSIDE UTHREAD_MALLOC\n");
		uthread_shared_info[i] = (uthread_shared_info_t *)(MALLOCZ_SAFE(sizeof(uthread_shared_info_t)));
		//uthread_shared_info[i] = (uthread_shared_info_t *)(malloc(sizeof(uthread_shared_info_t)));
		uthread_shared_info[i]->thread_id = -1;
		uthread_shared_info[i]->group_id = -1;
		uthread_shared_info[i]->elapsed_time = 0;
		uthread_shared_info[i]->uthread_credit = 0;
		uthread_shared_info[i]->initial_credit = 0;
		uthread_shared_info[i]->matrix_size = 0;
	}
	return;
}

extern void helper_sched_credit() 
{	
	printf("Inside helper\n");
	kthread_runqueue_t *kthread_runq;
	kthread_runq = &(kthread_cpu_map[kthread_apic_id()]->krunqueue);
	uthread_struct_t *cur_uthread = kthread_runq->cur_uthread;

	int credit1 = uthread_shared_info[cur_uthread->uthread_tid]->initial_credit;
	int interval_given;
//	if(credit1 == 0)
//		interval_given = KTHREAD_VTALRM_USEC;
//	else
		interval_given = KTHREAD_VTALRM_USEC*credit1/100;
	cur_uthread->elapsed_time = cur_uthread->elapsed_time + interval_given;
	cur_uthread->uthread_credit = 0;
	update_uthread_shared_statistics(cur_uthread);
}

extern void uthread_schedule(uthread_struct_t * (*kthread_best_sched_uthread)(kthread_runqueue_t *))
{
	//printf("Inside uthread_schedule\n");
	kthread_context_t *k_ctx;
	kthread_runqueue_t *kthread_runq;
	uthread_struct_t *u_obj;

	/* Signals used for cpu_thread scheduling */
	// kthread_block_signal(SIGVTALRM);
	// kthread_block_signal(SIGUSR1);

#if 0
	fprintf(stderr, "uthread_schedule invoked !!\n");
#endif

	k_ctx = kthread_cpu_map[kthread_apic_id()];
	kthread_runq = &(k_ctx->krunqueue);

	if((u_obj = kthread_runq->cur_uthread))
	{
		/*Go through the runq and schedule the next thread to run */
		kthread_runq->cur_uthread = NULL;
		
		if(u_obj->uthread_state & (UTHREAD_DONE | UTHREAD_CANCELLED))
		{	// TODO: For measuring uthread statistics. Do changes

			/* XXX: Inserting uthread into zombie queue is causing improper
			 * cleanup/exit of uthread (core dump) */
	//		uthread_head_t * kthread_zhead = &(kthread_runq->zombie_uthreads);
	//		gt_spin_lock(&(kthread_runq->kthread_runqlock));
	//		kthread_runq->kthread_runqlock.holder = 0x01;
	//		TAILQ_INSERT_TAIL(kthread_zhead, u_obj, uthread_runq);
	//		gt_spin_unlock(&(kthread_runq->kthread_runqlock));
		
		//	{
				ksched_shared_info_t *ksched_info = &ksched_shared_info;	
				gt_spin_lock(&ksched_info->ksched_lock);
				ksched_info->kthread_cur_uthreads--;
				gt_spin_unlock(&ksched_info->ksched_lock);
		//	}
		}
		else
		{
			#if 1
			{	// IF the thread is here, and is not over, then it either yielded the CPU or its credits has got over and it still needs processing. 
//1. 
				//int credits = u_obj->initial_credit;
				//u_obj->uthread_credit = credits;
				int credits = uthread_shared_info[u_obj->uthread_tid]->uthread_credit;
		
				if(credits > 0)
					add_to_runqueue(kthread_runq->active_runq, &(kthread_runq->kthread_runqlock), u_obj);
				else
				{	
					printf("Adding to underqueue, credits: %d, thread_id: %d\n", credits, u_obj->uthread_tid);
					add_to_runqueue(kthread_runq->expires_runq, &(kthread_runq->kthread_runqlock), u_obj);
				}

				//int credit1 = uthread_shared_info[u_obj->uthread_tid]->initial_credit;
				//u_obj->elapsed_time = u_obj->elapsed_time + (KTHREAD_VTALRM_USEC*credit1/100);
			}
			#endif

			u_obj->uthread_state = UTHREAD_RUNNABLE;

			#if 0
				add_to_runqueue(kthread_runq->expires_runq, &(kthread_runq->kthread_runqlock), u_obj);	
			#endif
		
			/* XXX: Save the context (signal mask not saved) */
			if(sigsetjmp(u_obj->uthread_env,0))
			{	return;
			}
		}
	}
	//printf("uthread_best_sched_uthread\n");
	/* kthread_best_sched_uthread acquires kthread_runqlock. Dont lock it up when calling the function. */
	if(!(u_obj = kthread_best_sched_uthread(kthread_runq)))
	{
	//	printf("uthreads finished\n");
		/* Done executing all uthreads. Return to main */
		/* XXX: We can actually get rid of KTHREAD_DONE flag */
		if(ksched_shared_info.kthread_tot_uthreads && !ksched_shared_info.kthread_cur_uthreads)
		{
			fprintf(stderr, "Quitting kthread (%d)\n", k_ctx->cpuid);
			k_ctx->kthread_flags |= KTHREAD_DONE;
		}

		siglongjmp(k_ctx->kthread_env, 1);
		return;
	}
	//printf("still uthreads remanining\n");
	//printf("kthread_runq: %p, uobj: %p\n", kthread_runq, u_obj);
	kthread_runq->cur_uthread = u_obj;
	//printf("After assiging\n");
	if((u_obj->uthread_state == UTHREAD_INIT) && (uthread_init(u_obj)))
	{	//printf("Inside if\n");
		fprintf(stderr, "uthread_init failed on kthread(%d)\n", k_ctx->cpuid);
		exit(0);
	}
//	printf("outside if\n");
	u_obj->uthread_state = UTHREAD_RUNNING;
	
	/* Re-install the scheduling signal handlers */
//	printf("getting credit info\n");
	int credit1 = uthread_shared_info[u_obj->uthread_tid]->uthread_credit;
//	printf("Uthread initial credit: %d\n", credit1);
	int time_interval = 0;
	if(credit1 == 0)
		time_interval = KTHREAD_VTALRM_USEC;
	else
		time_interval = KTHREAD_VTALRM_USEC*credit1/100;
	kthread_install_sighandler(SIGVTALRM, k_ctx->kthread_sched_timer);
	kthread_init_vtalrm_timeslice(0,0,0,time_interval);
	
	//time_t rawtime;
	//time(&rawtime);
	struct timeval *rawtime = (struct timeval *)MALLOC_SAFE(sizeof(struct timeval));
	gettimeofday(rawtime,NULL);
	//kthread_install_sighandler(SIGVTALRM, k_ctx->kthread_sched_timer);
	//kthread_install_sighandler(SIGUSR1, k_ctx->kthread_sched_relay);
	k_ctx->start_time = rawtime;
	//u_obj->start_time = rawtime;
	
	printf("Scheduling Thread: thread_id: %d, current_credits: %d, initial_credits: %d, start_time_sec: %d, start_time_usec: %d, interval_given: %d\n ",u_obj->uthread_tid, credit1, uthread_shared_info[u_obj->uthread_tid]->initial_credit, rawtime->tv_sec, rawtime->tv_usec, time_interval);
	
	siglongjmp(u_obj->uthread_env, 1);

	return;
}


/* For uthreads, we obtain a seperate stack by registering an alternate
 * stack for SIGUSR2 signal. Once the context is saved, we turn this 
 * into a regular stack for uthread (by using SS_DISABLE). */
static void uthread_context_func(int signo)
{
	//printf("Inside uthread_context_func\n");
	uthread_struct_t *cur_uthread;
	kthread_runqueue_t *kthread_runq;

	kthread_runq = &(kthread_cpu_map[kthread_apic_id()]->krunqueue);
	struct timeval finish_time;
	kthread_context_t *k_ctx = kthread_cpu_map[kthread_apic_id()];
	cur_uthread = kthread_runq->cur_uthread;
	//printf("..... uthread_context_func .....\n");
	/* kthread->cur_uthread points to newly created uthread */
	if(!sigsetjmp(kthread_runq->cur_uthread->uthread_env,0))
	{
		/* In UTHREAD_INIT : saves the context and returns.
		 * Otherwise, continues execution. */
		/* DONT USE any locks here !! */
		assert(kthread_runq->cur_uthread->uthread_state == UTHREAD_INIT);
		kthread_runq->cur_uthread->uthread_state = UTHREAD_RUNNABLE;
		return;
	}
	// SIGLONGJMP was executed
	assert(cur_uthread->uthread_state == UTHREAD_RUNNING);
	/* Execute the uthread task */	
	cur_uthread->uthread_func(cur_uthread->uthread_arg);
	gettimeofday(&finish_time, NULL);
	
	cur_uthread->uthread_state = UTHREAD_DONE;
	cur_uthread->finish_time_sec = (int)finish_time.tv_sec;
	cur_uthread->finish_time_usec = (int)finish_time.tv_usec;
//	int current_credits = cur_uthread->uthread_credit - 5;

//	uthread_shared_info[cur_uthread->uthread_tid]->uthread_credit = current_credits;
//	printf("current credits: %d\n", current_credits);

	if(finish_time.tv_sec < k_ctx->start_time->tv_sec)
		printf("Error calculating time finish_time less than start_time**************\n");
	int sec = (finish_time.tv_sec - k_ctx->start_time->tv_sec);
	int usec = 0;
	if(finish_time.tv_usec >= k_ctx->start_time->tv_usec) 
		usec = finish_time.tv_usec - k_ctx->start_time->tv_usec;
	else
	{	if(sec <=0)
			printf("Error: Seconds cant be less than 0******************\n");
		sec = sec-1;
		usec = (1000000 - k_ctx->start_time->tv_usec) + finish_time.tv_usec;
	}
	int elapsed_time_current = sec*1000000 + usec;
	//if(!elapsed_time_current)
	//{	elapsed_time_current = 1;
	//}
	int credit1 = uthread_shared_info[cur_uthread->uthread_tid]->uthread_credit;
	//printf("UTHREAD_initial_credit: %d\n", credit1);
	int interval_given;
	if(credit1 == 0)
		interval_given = KTHREAD_VTALRM_USEC;
	else
		interval_given = KTHREAD_VTALRM_USEC*credit1/100;
	
	cur_uthread->elapsed_time = cur_uthread->elapsed_time + elapsed_time_current;
	printf("UTHREAD_DONE, elapsed time: %d, start_time: %d, finish_time:%d credits:%d\n", cur_uthread->elapsed_time,
		(int)(k_ctx->start_time->tv_usec), (int)(finish_time.tv_usec), cur_uthread->uthread_credit);
	cur_uthread->uthread_credit = 0;
	update_uthread_shared_statistics(cur_uthread);

	if(elapsed_time_current > interval_given) 
	{		
		printf("************Error: elapsed_time_current > interval_given\n");
		printf("start_time_sec: %d, start_time_usec: %d, finish_time: %d, finish_time_usec: %d\n",
			(int)(k_ctx->start_time->tv_sec), (int)(k_ctx->start_time->tv_usec), 
			(int)(finish_time.tv_sec), (int)(finish_time.tv_usec));
	}
	//printf("Total execution time: %d, current execution time: %d, time_given: %d, initial_credits: %d, credits left: %d, thread_id: %d\n", 
	//	cur_uthread->elapsed_time, elapsed_time_current, interval_given, credit1, cur_uthread->uthread_credit, 
	//	cur_uthread->uthread_tid);
	printf("UTHREAD_DONE, thread_id: %d, initial_credits: %d, execution_start_time_sec: %d, execution_start_time_usec: %d, execution_finish_time_sec: %d, execution_finish_time_usec: %d, interval_given: %d, current execution time: %d, current_credits: %d\n", 
cur_uthread->uthread_tid, uthread_shared_info[cur_uthread->uthread_tid]->initial_credit, k_ctx->start_time->tv_sec, k_ctx->start_time->tv_usec, finish_time.tv_sec, finish_time.tv_usec, interval_given, elapsed_time_current, credit1);


	uthread_schedule(&sched_find_best_uthread);
	return;
}

extern void gt_yield() 
{
	printf("^^^^^^^^^^^^^^^Inside gt_yield^^^^^^^^^^^^^^\n");
	struct timeval finish_time;
	gettimeofday(&finish_time, NULL);
	uthread_struct_t *cur_uthread;
	kthread_runqueue_t *kthread_runq;
	

	kthread_runq = &(kthread_cpu_map[kthread_apic_id()]->krunqueue);
	kthread_context_t *k_ctx = kthread_cpu_map[kthread_apic_id()];
	cur_uthread = kthread_runq->cur_uthread;
	//cur_uthread->uthread_state = UTHREAD_DONE;
	if(finish_time.tv_sec < k_ctx->start_time->tv_sec)
	printf("Error calculating time finish_time less than start_time**************\n");
	int sec = (finish_time.tv_sec - k_ctx->start_time->tv_sec);
	int usec = 0;
	if(finish_time.tv_usec >= k_ctx->start_time->tv_usec) 
		usec = finish_time.tv_usec - k_ctx->start_time->tv_usec;
	else
	{	if(sec <=0)
			printf("Error: Seconds cant be less than 0******************\n");
		sec = sec-1;
		usec = (1000000 - k_ctx->start_time->tv_usec) + finish_time.tv_usec;
	}
	int elapsed_time_current = sec*1000000 + usec;
	int previous_uthread_credit = uthread_shared_info[cur_uthread->uthread_tid]->uthread_credit;
	int credit1 = uthread_shared_info[cur_uthread->uthread_tid]->uthread_credit;
	//printf("UTHREAD_initial_credit: %d\n", credit1);
	int interval_given = KTHREAD_VTALRM_USEC*previous_uthread_credit/100;
	if(elapsed_time_current > interval_given) 
	{		
		printf("************Error: elapsed_time_current > interval_given\n");
		printf("start_time_sec: %d, start_time_usec: %d, finish_time: %d, finish_time_usec: %d\n",
			(int)(k_ctx->start_time->tv_sec), (int)(k_ctx->start_time->tv_usec), 
			(int)(finish_time.tv_sec), (int)(finish_time.tv_usec));
	}
	if(elapsed_time_current == 0)
		elapsed_time_current = 1;
	float ratio=0.0;
	//if(interval_given == 0)
	//	ratio = 0.5;
	//else
		ratio = (((float)elapsed_time_current)/interval_given);
	int actual_credits_consumed =(int)(previous_uthread_credit*ratio);
	cur_uthread->uthread_credit = previous_uthread_credit - actual_credits_consumed;
	cur_uthread->elapsed_time = cur_uthread->elapsed_time + elapsed_time_current;
	
	printf("IN GT_YIELD, thread_id: %d, initial_credits: %d, elapsed time: %d, start_time_sec: %d, start_time_usec: %d, finish_time_sec: %d, finish_time_usec: %d, credits left: %d, interval_given: %d, ratio: %f, actual_credits_consumed: %d, credits left after gt_yield: %d\n", cur_uthread->uthread_tid, credit1, cur_uthread->elapsed_time, (int)(k_ctx->start_time->tv_sec), (int)(k_ctx->start_time->tv_usec), (int)(finish_time.tv_sec), (int)(finish_time.tv_usec),  uthread_shared_info[cur_uthread->uthread_tid]->uthread_credit, interval_given, ratio, actual_credits_consumed, cur_uthread->uthread_credit);


//	printf("IN GT_YIELD, thread_id: %d, elapsed time: %d, start_time: %d, finish_time:%d, credits:%d, actual_credits_consumed: %d, ratio: %f\n", cur_uthread->uthread_tid, cur_uthread->elapsed_time,
//		(int)(k_ctx->start_time->tv_usec), (int)(finish_time.tv_usec), cur_uthread->uthread_credit, actual_credits_consumed, ratio);
	update_uthread_shared_statistics(cur_uthread);

	//printf("Total execution time: %d, current execution time: %d, time_given: %d, initial_credits: %d, credits left: %d, thread_id: %d\n", 
	//	cur_uthread->elapsed_time, elapsed_time_current, interval_given, credit1, cur_uthread->uthread_credit, 
	//	cur_uthread->uthread_tid);

	uthread_schedule(&sched_find_best_uthread);
	return;	
}

void update_uthread_shared_statistics(uthread_struct_t *u_obj)
{
//	uthread_shared_info_t *shared_info = &uthread_shared_info;
	int thread_id = u_obj->uthread_tid;
	uthread_shared_info[thread_id]->thread_id = u_obj->uthread_tid;
	uthread_shared_info[thread_id]->group_id = u_obj->uthread_gid;
	uthread_shared_info[thread_id]->elapsed_time = u_obj->elapsed_time;
	uthread_shared_info[thread_id]->uthread_credit = u_obj->uthread_credit;
	uthread_shared_info[thread_id]->start_time_sec = u_obj->start_time_sec;
	uthread_shared_info[thread_id]->finish_time_sec = u_obj->finish_time_sec;
	uthread_shared_info[thread_id]->start_time_usec = u_obj->start_time_usec;
	uthread_shared_info[thread_id]->finish_time_usec = u_obj->finish_time_usec;
	return;
}

/**********************************************************************/
/* uthread creation */

extern kthread_runqueue_t *ksched_find_target(uthread_struct_t *);

extern int uthread_create(uthread_t *u_tid, int (*u_func)(void *), void *u_arg, uthread_group_t u_gid, int credit)
{
//	printf("Inside uthread_create\n");
	kthread_runqueue_t *kthread_runq;
	uthread_struct_t *u_new;

	/* Signals used for cpu_thread scheduling */
	// kthread_block_signal(SIGVTALRM);
	// kthread_block_signal(SIGUSR1);

	/* create a new uthread structure and fill it */
	if(!(u_new = (uthread_struct_t *)MALLOCZ_SAFE(sizeof(uthread_struct_t))))
	{
		fprintf(stderr, "uthread mem alloc failure !!");
		exit(0);
	}

	u_new->uthread_state = UTHREAD_INIT;
	u_new->uthread_priority = DEFAULT_UTHREAD_PRIORITY;
	//u_new->uthread_priority = u_arg->tid%4;
	u_new->uthread_gid = u_gid;
	u_new->uthread_func = u_func;
	u_new->uthread_arg = u_arg;
	u_new->elapsed_time = 0;
	//u_new->start_time = NULL;
	// Calculating credits for uthread.Credits can have the following values {25,50,75,100}

	
	/* Allocate new stack for uthread */
	u_new->uthread_stack.ss_flags = 0; /* Stack enabled for signal handling */
	if(!(u_new->uthread_stack.ss_sp = (void *)MALLOC_SAFE(UTHREAD_DEFAULT_SSIZE)))
	{
		fprintf(stderr, "uthread stack mem alloc failure !!");
		return -1;
	}
	u_new->uthread_stack.ss_size = UTHREAD_DEFAULT_SSIZE;

	{
		ksched_shared_info_t *ksched_info = &ksched_shared_info;

		gt_spin_lock(&ksched_info->ksched_lock);
		u_new->uthread_tid = ksched_info->kthread_tot_uthreads++;
		ksched_info->kthread_cur_uthreads++;
		gt_spin_unlock(&ksched_info->ksched_lock);
	}
//	int credit =  ((u_new->uthread_tid % 4)+1)*25;
	//printf("uthread_tid: %d, credit: %d\n", u_new->uthread_tid, credit);
	u_new->uthread_credit = credit;
	u_new->uthread_initial_credit = credit;
	struct timeval *start_time = (struct timeval *)MALLOC_SAFE(sizeof(struct timeval));
	gettimeofday(start_time, NULL);
	printf("Before alloting start time\n");
	u_new->start_time_sec = (int)(start_time->tv_sec);
	u_new->start_time_usec = (int)(start_time->tv_usec);
	printf("After alloting start time\n");
	//printf("uthread inital credit value:%d\n", u_new->uthread_initial_credit);
	uthread_shared_info[u_new->uthread_tid]->start_time_sec = (int)(start_time->tv_sec);
	uthread_shared_info[u_new->uthread_tid]->start_time_usec = (int)(start_time->tv_usec);
	uthread_shared_info[u_new->uthread_tid]->uthread_credit = credit;
	uthread_shared_info[u_new->uthread_tid]->initial_credit = credit;
	//printf("CREDITS: %d\n", uthread_shared_info[u_new->uthread_tid]->uthread_credit);
	/* XXX: ksched_find_target should be a function pointer */
	kthread_runq = ksched_find_target(u_new);

	*u_tid = u_new->uthread_tid;
	/* Queue the uthread for target-cpu. Let target-cpu take care of initialization. */
	add_to_runqueue(kthread_runq->active_runq, &(kthread_runq->kthread_runqlock), u_new);

	/* WARNING : DONOT USE u_new WITHOUT A LOCK, ONCE IT IS ENQUEUED. */

	/* Resume with the old thread (with all signals enabled) */
	// kthread_unblock_signal(SIGVTALRM);
	// kthread_unblock_signal(SIGUSR1);

	return 0;
}

#if 0
/**********************************************************************/
kthread_runqueue_t kthread_runqueue;
kthread_runqueue_t *kthread_runq = &kthread_runqueue;
sigjmp_buf kthread_env;

/* Main Test */
typedef struct uthread_arg
{
	int num1;
	int num2;
	int num3;
	int num4;	
} uthread_arg_t;

#define NUM_THREADS 10
static int func(void *arg);

int main()
{
	uthread_struct_t *uthread;
	uthread_t u_tid;
	uthread_arg_t *uarg;

	int inx;

	/* XXX: Put this lock in kthread_shared_info_t */
	gt_spinlock_init(&uthread_group_penalty_lock);

	/* spin locks are initialized internally */
	kthread_init_runqueue(kthread_runq);

	for(inx=0; inx<NUM_THREADS; inx++)
	{
		uarg = (uthread_arg_t *)MALLOC_SAFE(sizeof(uthread_arg_t));
		uarg->num1 = inx;
		uarg->num2 = 0x33;
		uarg->num3 = 0x55;
		uarg->num4 = 0x77;
		uthread_create(&u_tid, func, uarg, (inx % MAX_UTHREAD_GROUPS));
	}

	kthread_init_vtalrm_timeslice();
	kthread_install_sighandler(SIGVTALRM, kthread_sched_vtalrm_handler);
	if(sigsetjmp(kthread_env, 0) > 0)
	{
		/* XXX: (TODO) : uthread cleanup */
		exit(0);
	}
	
	uthread_schedule(&ksched_priority);
	return(0);
}

static int func(void *arg)
{
	unsigned int count;
#define u_info ((uthread_arg_t *)arg)
	printf("Thread %d created\n", u_info->num1);
	count = 0;
	while(count <= 0xffffff)
	{
		if(!(count % 5000000))
			printf("uthread(%d) => count : %d\n", u_info->num1, count);
		count++;
	}
#undef u_info
	return 0;
}
#endif
