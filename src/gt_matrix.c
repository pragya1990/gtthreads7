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
#include "gt_include.h"
#include <stdlib.h>
#include <math.h>
//#define ROWS 512
//#define COLS ROWS
//#define SIZE COLS

//#define NUM_CPUS 2
//#define NUM_GROUPS NUM_CPUS
//#define PER_GROUP_COLS (SIZE/NUM_GROUPS)

//#define MATRIX_SIZES_COUNT 4
//#define CREDIT_TYPES_COUNT 4
//#define TOTAL_GROUPS (MATRIX_SIZES_COUNT*CREDIT_TYPES_COUNT)
//#define THREADS_PER_GROUP (NUM_THREADS/TOTAL_GROUPS)

//static int matrix_sizes[MATRIX_SIZES_COUNT] = {4,8,12,16};
static int matrix_sizes[MATRIX_SIZES_COUNT] = {32,64,128,256};
static int credit_values[CREDIT_TYPES_COUNT] = {25,50,75,100};
//static uthread_shared_info_t *uthread_shared_info[NUM_THREADS];
extern void print_statistics();
void init_group_stats();
 void print_group_stats();
typedef struct group
{	int matrix_size;
	int credit_value;
	int group_id;
	
} group_t;

typedef struct _group_stats
{
	int matrix_size;
	int credit_value;
	int group_id;
	unsigned long long execution_time;
	unsigned long long total_run_time;
	unsigned long long mean_exec_time;
	unsigned long long mean_run_time;
	unsigned long long st_dev_exec_time;
	unsigned long long st_dev_run_time;
	unsigned long long sum_exec_time;
	unsigned long long sum_run_time;
	unsigned long long square_sum_exec_time;
	unsigned long long square_sum_run_time;
} group_stats_t;

group_t possible_groups[TOTAL_GROUPS];
group_stats_t group_stats[TOTAL_GROUPS];

/* A[SIZE][SIZE] X B[SIZE][SIZE] = C[SIZE][SIZE]
 * Let T(g, t) be thread 't' in group 'g'. 
 * T(g, t) is responsible for multiplication : 
 * A(rows)[(t-1)*SIZE -> (t*SIZE - 1)] X B(cols)[(g-1)*SIZE -> (g*SIZE - 1)] */

typedef struct matrix
{
	//int m[SIZE][SIZE];
	int *m;
	int rows;
	int cols;
	group_t *matrix_group;
	unsigned int reserved[2];
} matrix_t;

typedef struct __uthread_arg
{
	matrix_t *_A, *_B, *_C;
	unsigned int size;

	unsigned int tid;
	unsigned int gid;
	int start_row; /* start_row -> (start_row + PER_THREAD_ROWS) */
	int start_col; /* start_col -> (start_col + PER_GROUP_COLS) */
	int end_row;
	int end_col;	
} uthread_arg_t;

static void generate_matrix(matrix_t *mat, int val, int size)
{
	int i,j;
	mat->rows = size;
	mat->cols = size;
	for(i = 0; i < mat->rows;i++)
		for( j = 0; j < mat->cols; j++ )
		{
			//mat->m[i][j] = val;
			mat->m[i*size + j] = val;
		}
	return;
}

static void print_matrix(matrix_t *mat, int size)
{
	int i, j;
	for(i=0;i<size;i++)
	{
		for(j=0;j<size;j++)
			printf("%d ",mat->m[i*size + j]);
		printf("\n");
	}

	return;
}

static void * uthread_mulmat(void *p)
{
	int i, j, k;
	int start_row, end_row;
	int start_col, end_col;
	unsigned int cpuid;
	struct timeval tv2;

#define ptr ((uthread_arg_t *)p)

	i=0; j= 0; k=0;

	start_row = ptr->start_row;
	end_row = ptr->end_row;

	start_col = ptr->start_col;
	end_col = ptr->end_col;
	int size = ptr->_A->matrix_group->matrix_size;
	
/*#ifdef GT_GROUP_SPLIT
	start_col = ptr->start_col;
	end_col = ptr->end_col;
#else
	start_col = 0;
	end_col = size;
#endif*/
/*
#ifdef GT_THREADS
	cpuid = kthread_cpu_map[kthread_apic_id()]->cpuid;
	fprintf(stderr, "Thread(id:%d, group:%d, cpu:%d) started\n",ptr->tid, ptr->gid, cpuid);
#else
	fprintf(stderr, "Thread(id:%d, group:%d) started\n",ptr->tid, ptr->gid);
#endif
*/
	for(i = start_row; i < end_row; i++)
		for(j = start_col; j < end_col; j++)
			for(k = 0; k < size; k++)
				ptr->_C->m[i*size + j] += ptr->_A->m[i*size + k] * ptr->_B->m[k*size + j];
	if(ptr->tid == 12)
	{	
		printf("Going to execute gt_yield()\n");
		gt_yield();
		//return;
	}


/*
#ifdef GT_THREADS
	gettimeofday(&tv2,NULL);
	fprintf(stderr, "Thread(id:%d, group:%d, cpu:%d) finished (TIME : %lu s and %lu us)\n",
			ptr->tid, ptr->gid, cpuid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));
#else
	gettimeofday(&tv2,NULL);
	fprintf(stderr, "Thread(id:%d, group:%d) finished (TIME : %lu s and %lu us)\n",
			ptr->tid, ptr->gid, (tv2.tv_sec - tv1.tv_sec), (tv2.tv_usec - tv1.tv_usec));
#endif
*/
#undef ptr
	return 0;
}

//matrix_t A, B, C;
matrix_t *matrices_A[TOTAL_GROUPS];
matrix_t *matrices_B[TOTAL_GROUPS];
matrix_t *matrices_C[TOTAL_GROUPS];

static void init_matrices(group_t *grp)
{
	int size = grp->matrix_size;
	int index = grp->group_id;
	int credit = grp->credit_value;

	matrices_A[index] = (matrix_t*)MALLOCZ_SAFE(sizeof(matrix_t));
	matrices_B[index] = (matrix_t*)MALLOCZ_SAFE(sizeof(matrix_t));
	matrices_C[index] = (matrix_t*)MALLOCZ_SAFE(sizeof(matrix_t));
	
	matrices_A[index]->m = (int *)MALLOCZ_SAFE((sizeof(int))*size*size); 
	matrices_B[index]->m = (int *)MALLOCZ_SAFE((sizeof(int))*size*size);
	matrices_C[index]->m = (int *)MALLOCZ_SAFE((sizeof(int))*size*size);

	group_t *grp_A = (group_t *)MALLOCZ_SAFE(sizeof(group_t));	
	group_t *grp_B = (group_t *)MALLOCZ_SAFE(sizeof(group_t));	
	group_t *grp_C = (group_t *)MALLOCZ_SAFE(sizeof(group_t));
	
	grp_A->matrix_size = grp_B->matrix_size = grp_C->matrix_size = size;
	grp_A->credit_value = grp_B->credit_value = grp_C->credit_value = credit;
	grp_A->group_id = grp_B->group_id = grp_C->group_id = index;

	matrices_A[index]->matrix_group = grp_A;
	matrices_B[index]->matrix_group = grp_B;
	matrices_C[index]->matrix_group = grp_C;
	
	generate_matrix(matrices_A[index], 1, size);
	generate_matrix(matrices_B[index], 1, size);
	generate_matrix(matrices_C[index], 0, size);

	return;
}

uthread_arg_t uargs[NUM_THREADS];
uthread_t utids[NUM_THREADS];

void init_possible_groups()
{
	int i=0, j=0, counter=0;
	for(i=0;i<MATRIX_SIZES_COUNT;i++)
	{
		for(j=0;j<CREDIT_TYPES_COUNT;j++)
		{	
			possible_groups[counter].matrix_size = matrix_sizes[i];
			possible_groups[counter].credit_value = credit_values[j];
			possible_groups[counter].group_id = counter;
			counter++;
		}
	}
}

void verify_answer() 
{
	int i, j, k, size;
	for(i=0;i<TOTAL_GROUPS;i++) 
	{	matrix_t *m1 = matrices_C[i];
		size = m1->matrix_group->matrix_size;
		for(j=0;j<size;j++) 
		{	for(k=0;k<size;k++) 
			{	
				if(m1->m[j*size + k] == size)
					continue;
				else
				{	printf("****Error:Answers of matrix multiplication are wrong!!***\n");
					return;
				}
			}	
		}
	}
	printf("Matrix multiplication is correct.\n");
}

//int main(int argc, char *argv[])
int main()
{
/*	isCreditScheduler = 0;
	if(argc > 1)
		if(*argv[1] == '1')
			isCreditScheduler = 1;	
*/	
	isCreditScheduler = 1;
	uthread_arg_t *uarg;
	int inx,i;
	
//	kthread_block_signal(SIGVTALRM);
//	kthread_block_signal(SIGUSR1);
	gtthread_app_init();
	uthread_info_init(); // initialises the uthread info that is shared among all uthreads. Used for logging purposes.

	init_possible_groups();
	for(i=0;i<TOTAL_GROUPS;i++)
		init_matrices(&possible_groups[i]);

	//int current_matrix_size_index, current_row_index, current_credit_value_index, current_num_thread_per_group_index, current_num_group_index;
	int size, rows_per_thread, current_row = 0, group_id = 0;
	for(inx=0; inx<NUM_THREADS; inx++)
	{
		size = possible_groups[group_id].matrix_size;
		rows_per_thread = size / (THREADS_PER_GROUP);
		
		uarg = &uargs[inx];
		uarg->_A = matrices_A[group_id];
		uarg->_B = matrices_B[group_id];
		uarg->_C = matrices_C[group_id];

		uarg->tid = inx;
		uarg->gid = group_id;
		uarg->start_row = current_row;
		uarg->end_row = current_row + rows_per_thread;
		uarg->start_col = 0;
		uarg->end_col = size;
		int credit = matrices_A[group_id]->matrix_group->credit_value;

		printf("group_id: %d, current_row: %d, rows_per_thread: %d, size: %d, credits: %d, inx: %d, NUM_THREADS: %d\n", 
			group_id, current_row, rows_per_thread, size,  credit, inx, NUM_THREADS);

		current_row = current_row + rows_per_thread;
		if(current_row == size)
		{
			current_row = 0;
			group_id++;
		}
//#ifdef GT_GROUP_SPLIT
		// Wanted to split the columns by groups !!! *
		//uarg->start_col = (uarg->gid * PER_GROUP_COLS);
//#endif
	//	printf("going to uthread_create\n");
		uthread_create(&utids[inx], uthread_mulmat, uarg, uarg->gid, credit);
	//`	printf("exit from uthread_create\n");
	}
	
//	kthread_unblock_signal(SIGVTALRM);
//	kthread_unblock_signal(SIGUSR1);
	FILE *fp, *fp1;
	fp = fopen("stat.txt","w+");
	fp1 = fopen("stat1.txt","w+");
	gtthread_app_exit();
	print_statistics(fp,fp1);
	init_group_stats();
	print_group_stats(fp,fp1);
	verify_answer();
	fclose(fp);
	fclose(fp1);

//	for(i=0;i<TOTAL_GROUPS;i++)
//	{
//		int size = possible_groups[i].matrix_size;
//		printf("Matrix A, i:%d, size: %d, credits: %d\n", i, size, possible_groups[i].credit_value);
//		print_matrix(matrices_A[i], size);
//		printf("Matrix B, i:%d, size: %d, credits: %d\n", i, size, possible_groups[i].credit_value);
//		print_matrix(matrices_B[i], size);
//		printf("Matrix C, i:%d, size: %d, credits: %d\n", i, size, possible_groups[i].credit_value);
//		print_matrix(matrices_C[i], size);
//	}

	// print_matrix(&C);
	// fprintf(stderr, "********************************");
	return(0);
}

void print_group_stats(FILE *fp, FILE *fp1)
{
	int i;
	for(i=0;i<NUM_THREADS;i++)
	{
		uthread_shared_info_t *cur_t = uthread_shared_info[i];
		int tid = cur_t->group_id;
		group_stats[tid].sum_run_time = group_stats[tid].sum_run_time + cur_t->total_run_time;
		group_stats[tid].sum_exec_time = group_stats[tid].sum_exec_time + cur_t->elapsed_time;
	}
	
	for(i=0;i<TOTAL_GROUPS;i++)
	{	
		group_stats[i].mean_run_time = (int)(group_stats[i].sum_run_time)/(THREADS_PER_GROUP);
		group_stats[i].mean_exec_time = (int)(group_stats[i].sum_exec_time)/(THREADS_PER_GROUP);
	}
	
	for(i=0;i<NUM_THREADS;i++)
	{
		
		uthread_shared_info_t *cur_t = uthread_shared_info[i];
		int tid = cur_t->group_id;

		unsigned long long temp_val = group_stats[tid].mean_run_time - cur_t->total_run_time;
		group_stats[tid].square_sum_run_time = group_stats[tid].square_sum_run_time + (temp_val * temp_val);

		unsigned long long temp_val1 = group_stats[tid].mean_exec_time - cur_t->elapsed_time;
		group_stats[tid].square_sum_exec_time = group_stats[tid].square_sum_exec_time + (temp_val1 * temp_val1);
	}
	
	for(i=0;i<TOTAL_GROUPS;i++)
	{	
		group_stats[i].st_dev_run_time = sqrt((group_stats[i].square_sum_run_time)/(THREADS_PER_GROUP));
		group_stats[i].st_dev_exec_time = sqrt((group_stats[i].square_sum_exec_time)/(THREADS_PER_GROUP));
	}

	fprintf(fp1, "group_id   matrix_size   initial_credits   mean_run_time    mean_exec_time   st_dev_run_time   st_dev_exec_time\n");
	for(i=0;i<TOTAL_GROUPS;i++)
	{
		group_stats_t grp = group_stats[i];
		fprintf(fp, "group_id: %d, matrix_size: %d, initial_credits: %d, mean_run_time: %llu, mean_exec_time: %llu, st_dev_run_time: %llu, st_dev_exec_time: %llu\n", grp.group_id, grp.matrix_size, grp.credit_value, grp.mean_run_time, grp.mean_exec_time, grp.st_dev_run_time, grp.st_dev_exec_time);
		fprintf(fp1, "%d              %d                %d           %llu             %llu              %llu             %llu\n", grp.group_id, grp.matrix_size, grp.credit_value, grp.mean_run_time, grp.mean_exec_time, grp.st_dev_run_time, grp.st_dev_exec_time);
	}
	
}

void init_group_stats() 
{	
	int i;
	for(i=0;i<TOTAL_GROUPS;i++)
	{
		group_stats[i].matrix_size = possible_groups[i].matrix_size;
		group_stats[i].credit_value = possible_groups[i].credit_value;
		group_stats[i].group_id = possible_groups[i].group_id;
		group_stats[i].mean_exec_time = 0;
		group_stats[i].mean_run_time = 0;
		group_stats[i].st_dev_exec_time = 0;
		group_stats[i].st_dev_run_time = 0;
		group_stats[i].sum_run_time = 0;
		group_stats[i].sum_exec_time = 0;
		group_stats[i].square_sum_run_time = 0;
		group_stats[i].square_sum_exec_time = 0;
	}
}

extern void print_statistics(FILE *fp, FILE *fp1) 
{
	printf("**************** Printing statistics ************\n");
	int i;
	int start_time_sec;
	int start_time_usec;
	int finish_time_sec;
	int finish_time_usec; 

	fprintf(fp1,"thread id   group_id   matrix_size   initial_credits   Run_time   Execution_time\n");
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
		
		fprintf(fp,"thread id: %d, group_id: %d, matrix_size: %d, initial_credits: %d, Run_time: %d, Execution_time: %d\n",uthread_shared_info[i]->thread_id, uthread_shared_info[i]->group_id, uthread_shared_info[i]->matrix_size, uthread_shared_info[i]->initial_credit, uthread_shared_info[i]->total_run_time, uthread_shared_info[i]->elapsed_time);
		
		fprintf(fp1,"%d             %d              %d               %d          %d            %d\n", uthread_shared_info[i]->thread_id, uthread_shared_info[i]->group_id, uthread_shared_info[i]->matrix_size, uthread_shared_info[i]->initial_credit, uthread_shared_info[i]->total_run_time, uthread_shared_info[i]->elapsed_time);
	}
	return;
}
