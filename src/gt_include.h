#ifndef __GT_INCLUDE_H
#define __GT_INCLUDE_H

#include "gt_signal.h"
#include "gt_spinlock.h"
#include "gt_tailq.h"
#include "gt_bitops.h"

#include "gt_uthread.h"
#include "gt_pq.h"
#include "gt_kthread.h"

#define NUM_THREADS 128
#define MATRIX_SIZES_COUNT 4
#define CREDIT_TYPES_COUNT 4
#define TOTAL_GROUPS (MATRIX_SIZES_COUNT*CREDIT_TYPES_COUNT)
#define THREADS_PER_GROUP (NUM_THREADS/TOTAL_GROUPS)

int isCreditScheduler;
#endif
