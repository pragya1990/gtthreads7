#ifndef __GT_SIGNAL_H
#define __GT_SIGNAL_H

/**********************************************************************/
/* kthread signal handling */
extern void kthread_install_sighandler(int signo, void (*handler)(int));
extern void kthread_block_signal(int signo);
extern void kthread_unblock_signal(int signo);

#define KTHREAD_VTALRM_SEC 0
#define KTHREAD_VTALRM_USEC 500
extern void kthread_init_vtalrm_timeslice(int interval_sec, int interval_usec, int value_sec, int value_usec);

#endif
