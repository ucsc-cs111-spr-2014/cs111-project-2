/* This file contains the scheduling policy for SCHED
 *
 * The entry points are:
 *   do_noquantum:        Called on behalf of process' that run out of quantum
 *   do_start_scheduling  Request to start scheduling a proc
 *   do_stop_scheduling   Request to stop scheduling a proc
 *   do_nice		  Request to change the nice level on a proc
 *   init_scheduling      Called from main.c to set up/prepare scheduling
 */
#include "sched.h"
#include "schedproc.h"
#include <assert.h>
#include <minix/com.h>
#include <machine/archtypes.h>
#include "kernel/proc.h" /* for queue constants */

PRIVATE timer_t sched_timer;
PRIVATE unsigned balance_timeout;

#define BALANCE_TIMEOUT	5 /* how often to balance queues in seconds */
#define NR_TIX_DEFAULT 20
#define LOSER_PR 15
#define WINNER_PR 14
#define LOTTERY_PRINT 1
#define DEFAULT_USER_TIME_SLICE 200

FORWARD _PROTOTYPE( int schedule_process, (struct schedproc * rmp)	);
/*FORWARD _PROTOTYPE( void balance_queues, (struct timer *tp)		);*/
int do_lottery(void);

/*===========================================================================*
 *				do_noquantum				     *
 *===========================================================================*/

PUBLIC int do_noquantum(message *m_ptr)
{
	register struct schedproc *rmp;
	int rv, proc_nr_n;
	
	/*printf("CMPS111 DO NO QUANTUM\n");*/	

	if (sched_isokendpt(m_ptr->m_source, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg %u.\n",
		m_ptr->m_source);
		return EBADEPT;
	}

	rmp = &schedproc[proc_nr_n];
	rmp->priority = LOSER_PR;
	if ((rv = schedule_process(rmp)) != OK) {
		return rv;
	}
	do_lottery();
	return OK;
}

/*===========================================================================*
 *				do_stop_scheduling			     *
 *===========================================================================*/
PUBLIC int do_stop_scheduling(message *m_ptr)
{
	register struct schedproc *rmp;
	int rv, proc_nr_n;

	/*printf("CMPS111 DO STOP SCHEDULING\n");*/

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	if (sched_isokendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg "
		"%ld\n", m_ptr->SCHEDULING_ENDPOINT);
		return EBADEPT;
	}

	
	rmp = &schedproc[proc_nr_n];
	rmp->priority = LOSER_PR;
	rmp->flags = 0; /*&= ~IN_USE;*/
	do_lottery();
	return OK;
}

/*===========================================================================*
 *				do_start_scheduling			     *
 *===========================================================================*/
PUBLIC int do_start_scheduling(message *m_ptr)
{
	register struct schedproc *rmp;
	int rv, proc_nr_n, parent_nr_n, nice;
	
	/*printf("CMPS111 DO START SCHEDULING\n");*/

	/* we can handle two kinds of messages here */
	assert(m_ptr->m_type == SCHEDULING_START || 
		m_ptr->m_type == SCHEDULING_INHERIT);

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	/* Resolve endpoint to proc slot. */
	if ((rv = sched_isemtyendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n))
			!= OK) {
		return rv;
	}
	rmp = &schedproc[proc_nr_n];

	/* Populate process slot */
	rmp->endpoint     = m_ptr->SCHEDULING_ENDPOINT;
	rmp->parent       = m_ptr->SCHEDULING_PARENT;
	rmp->max_priority = (unsigned) m_ptr->SCHEDULING_MAXPRIO;
	if (rmp->max_priority >= NR_SCHED_QUEUES) {
		return EINVAL;
	}
	
	switch (m_ptr->m_type) {

	case SCHEDULING_START:
		/* We have a special case here for system processes, for which
		 * quanum and priority are set explicitly rather than inherited 
		 * from the parent */

/*		printf("CMPS111 SCHEDULING START\n");*/
		rmp->priority   = LOSER_PR;
		rmp->time_slice = (unsigned) m_ptr->SCHEDULING_QUANTUM;
		rmp->num_tix = (unsigned) NR_TIX_DEFAULT;
		break;
		
	case SCHEDULING_INHERIT:
		/* Inherit current priority and time slice from parent. Since there
		 * is currently only one scheduler scheduling the whole system, this
		 * value is local and we assert that the parent endpoint is valid */
		/*printf("CMPS111 SCHEDULING INHERIT");*/
		if ((rv = sched_isokendpt(m_ptr->SCHEDULING_PARENT,
				&parent_nr_n)) != OK)
			return rv;
		rmp->num_tix = schedproc[parent_nr_n].num_tix;
		rmp->priority = schedproc[parent_nr_n].priority;
		rmp->time_slice = schedproc[parent_nr_n].time_slice;
		break;
		
	default: 
		/* not reachable */
		assert(0);
	}

	/* Take over scheduling the process. The kernel reply message populates
	 * the processes current priority and its time slice */
	if ((rv = sys_schedctl(0, rmp->endpoint, 0, 0)) != OK) {
		printf("Sched: Error taking over scheduling for %d, kernel said %d\n",
			rmp->endpoint, rv);
		return rv;
	}
	rmp->flags = IN_USE;

	/* Schedule the process, giving it some quantum */
	if ((rv = schedule_process(rmp)) != OK) {
		printf("Sched: Error while scheduling process, kernel replied %d\n",
			rv);
		return rv;
	}

	/* Mark ourselves as the new scheduler.
	 * By default, processes are scheduled by the parents scheduler. In case
	 * this scheduler would want to delegate scheduling to another
	 * scheduler, it could do so and then write the endpoint of that
	 * scheduler into SCHEDULING_SCHEDULER
	 */

	m_ptr->SCHEDULING_SCHEDULER = SCHED_PROC_NR;

	return OK;
}

/*===========================================================================*
 *				do_nice					     *
 *===========================================================================*/
PUBLIC int do_nice(message *m_ptr)
{
	struct schedproc *rmp, *loop_rmp;
	int rv;
	int proc_nr_n, proc_nr;
	unsigned new_q, old_q, old_max_q, new_num_tix, old_num_tix;


	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	if (sched_isokendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg "
		"%ld\n", m_ptr->SCHEDULING_ENDPOINT);
		return EBADEPT;
	}

	rmp = &schedproc[proc_nr_n];

	printf("MAXPRIO:%d\n", m_ptr->SCHEDULING_MAXPRIO);
	new_num_tix = rmp->num_tix + m_ptr->SCHEDULING_MAXPRIO;
	
	/* print number of tix for each process */
    for(proc_nr=0, loop_rmp=schedproc; proc_nr < NR_PROCS; proc_nr++, loop_rmp++) {
            if ((loop_rmp->flags & IN_USE) && (loop_rmp->priority >= MAX_USER_Q) &&
                     (loop_rmp->priority <= MIN_USER_Q)) {
                    if (USER_Q == loop_rmp->priority) {
                            printf("proc endpt:%d\tpri:%d\tix:%d\t\n", 
                            	loop_rmp->endpoint, loop_rmp->priority, loop_rmp->num_tix);
                    }
            }
    }
	printf("DOING NICE\n");


	/* Store old values, in case we need to roll back the changes */
	/*old_q     = rmp->priority;
	old_max_q = rmp->max_priority;*/
	old_num_tix = rmp->num_tix;

	/* Update the proc entry and reschedule the process */
	rmp->num_tix = new_num_tix;

	if ((rv = schedule_process(rmp)) != OK) {
		/* Something went wrong when rescheduling the process, roll
		 * back the changes to proc struct */
		rmp->num_tix = old_num_tix;
	}

	return rv;
}

/*===========================================================================*
 *				schedule_process			     *
 *===========================================================================*/
PRIVATE int schedule_process(struct schedproc * rmp)
{
	int rv;

/*	printf("CMPS111 SCHEDULE PROCESS\n");*/
	if(LOTTERY_PRINT)
		printf("schedule_process endpoint:%d\n", rmp->endpoint);
	if ((rv = sys_schedule(rmp->endpoint, rmp->priority,
			rmp->time_slice)) != OK) {
		printf("SCHED: An error occurred when trying to schedule %d: %d\n",
		rmp->endpoint, rv);
	}

	return rv;
}


/*===========================================================================*
 *				start_scheduling			     *
 *===========================================================================*/

PUBLIC void init_scheduling(void)
{

/*	printf("CMPS111 START SCHEDULING");
	balance_timeout = BALANCE_TIMEOUT * sys_hz();
	init_timer(&sched_timer);
	set_timer(&sched_timer, balance_timeout, balance_queues, 0);*/

}

/*===========================================================================*
 *				balance_queues				     *
 *===========================================================================*/

/* This function is called every 100 ticks to rebalance the queues. The current
 * scheduler bumps processes down one priority when ever they run out of
 * quantum. This function will find all proccesses that have been bumped down,
 * and pulls them back up. This default policy will soon be changed.
 
PRIVATE void balance_queues(struct timer *tp)
{
	struct schedproc *rmp;
	int proc_nr;
	int rv;

/*	printf("CMPS111 BALANCE QUEUES\n");

	for (proc_nr=0, rmp=schedproc; proc_nr < NR_PROCS; proc_nr++, rmp++) {
		if (rmp->flags & IN_USE) {
			if (rmp->priority > rmp->max_priority) {
				rmp->priority -= 1; /* increase priority 
				schedule_process(rmp);
			}
		}
	}

	set_timer(&sched_timer, balance_timeout, balance_queues, 0);
}*/

/*=============================================================================*
 *                                do_lottery                                   *
 *=============================================================================*/
PUBLIC int do_lottery(void)
{
	struct schedproc *rmp;
	int proc_nr;
	int numTixTot;
	int winner;
	int rv;

	/* sum number of tickets in each process */
	for(proc_nr=0, rmp=schedproc; proc_nr < NR_PROCS; proc_nr++, rmp++) {
		if ((rmp->flags & IN_USE) && (rmp->priority >= MAX_USER_Q) &&
			 (rmp->priority <= MIN_USER_Q)) {
			if (USER_Q == rmp->priority) {
				numTixTot += rmp->num_tix;
			}
		}
	}
	
	/* pick a winning ticket */
	winner = rand() % numTixTot;
	
	/* determine owner of winning ticket */
	for (proc_nr=0, rmp=schedproc; proc_nr < NR_PROCS; proc_nr++, rmp++) {
		if (rmp->flags & IN_USE && rmp->num_tix > 0) {
			if (rmp->num_tix < winner) {
				winner -= rmp->num_tix;
			} else { 
				break;/* found the winner! */
			}
		}
	}
	
	/* printing for my peace of mind that this lottery actually determines the 
	   next process. Check the readme to see some of my output */
	if(LOTTERY_PRINT)
		printf("numTixTot:%d\twinner:%d\tendpt:%d\n", numTixTot, winner, rmp->endpoint);

	/* schedule the lottery winner */
	rmp->priority = WINNER_PR;
	rmp->num_tix--;
	rmp->time_slice = USER_QUANTUM;
	if ((rv = schedule_process(rmp)) != OK) {
                printf("Sched: Error while scheduling process, kernel replied %d\n", rv);
                return rv;
        }

	return OK;
}
