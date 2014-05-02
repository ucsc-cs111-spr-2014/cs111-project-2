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
PRIVATE unsigned NR_TICKETS;

#define NR_TIX_DEFAULT 20 /*todo should also be default max_tix, unless do_nice*/
#define MIN_NR_TIX 1

#define LOSER_PR 15
#define WINNER_PR 14

#define LOTTERY_PRINT 1

FORWARD _PROTOTYPE( int schedule_process, (struct schedproc * rmp, char *tag)	);
/*FORWARD _PROTOTYPE( void balance_queues, (struct timer *tp)		);*/
int do_lottery(void);
int do_changetickets(struct schedproc *rmp, int val);

/*move printing and getwinner to utlity.c?*/
PRIVATE void do_print_process(struct schedproc *temp_rmp, char* tag)
{
	if (!LOTTERY_PRINT) {
		return;
	}
	if (!(temp_rmp->flags & IN_USE)) {
		return;
	}
	printf("%s::proc endpt:%5d pri:%2d tix:%3d\n", 
		tag, temp_rmp->endpoint, temp_rmp->priority, temp_rmp->num_tix);
}

PRIVATE void do_print_user_queues(char *tag)
{
	struct schedproc *loop_rmp;
	int proc_nr;

	if (!LOTTERY_PRINT) {
		return;
	}

	/* print number of tix for each process */
	for (proc_nr=0, loop_rmp=schedproc; proc_nr < NR_PROCS; proc_nr++, loop_rmp++) {
		if ((loop_rmp->flags & IN_USE) && (loop_rmp->priority >= MAX_USER_Q) &&
				(loop_rmp->priority <= MIN_USER_Q)) {
			if (1) {/*USER_Q == loop_rmp->priority*/
				do_print_process(loop_rmp, tag);
			}
		}
	}
}

PRIVATE struct schedproc *get_winner()
{
	struct schedproc *rmp;
	int proc_nr;

	/*find and return process in WINNER_PR*/
	for (proc_nr=0, rmp=schedproc; proc_nr < NR_PROCS; proc_nr++, rmp++) {
		if (rmp->flags & IN_USE && rmp->num_tix > 0) {
			if (rmp->priority == WINNER_PR) { 
				do_print_process(rmp, "get_winner");
				break;/* found the winner! */
			}
		}
	}

	return rmp;
}

/*===========================================================================*
 *				do_noquantum				     *
 *===========================================================================*/

PUBLIC int do_noquantum(message *m_ptr)
{
	register struct schedproc *rmp;
	int err, proc_nr_n;
	
	/*printf("CMPS111 DO NO QUANTUM\n");*/	

	if (sched_isokendpt(m_ptr->m_source, &proc_nr_n) != OK) {
		printf("SCHED: WARNING: got an invalid endpoint in OOQ msg %u.\n",
		m_ptr->m_source);
		return EBADEPT;
	}

	rmp = &schedproc[proc_nr_n];
	if (rmp->priority == WINNER_PR) {
		rmp->num_tix--;
	} else if (rmp->priority == LOSER_PR) {
		rmp = get_winner();
		rmp->num_tix++;
	}
	rmp->priority = LOSER_PR;
	if ((err = schedule_process(rmp, "do_noquantum")) != OK) {
		return err;
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
	int err, proc_nr_n;

	printf("CMPS111 DO STOP SCHEDULING\n");

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
	int err, proc_nr_n, parent_nr_n, nice;
	
	/*printf("CMPS111 DO START SCHEDULING\n");*/

	/* we can handle two kinds of messages here */
	assert(m_ptr->m_type == SCHEDULING_START || 
		m_ptr->m_type == SCHEDULING_INHERIT);

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	/* Resolve endpoint to proc slot. */
	if ((err = sched_isemtyendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n))
			!= OK) {
		return err;
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
		if ((err = sched_isokendpt(m_ptr->SCHEDULING_PARENT,
				&parent_nr_n)) != OK)
			return err;
		
		rmp->priority   = LOSER_PR;
		rmp->num_tix = schedproc[parent_nr_n].num_tix;
		rmp->time_slice = schedproc[parent_nr_n].time_slice;
		break;
		
	default: 
		/* not reachable */
		assert(0);
	}

	/* Take over scheduling the process. The kernel reply message populates
	 * the processes current priority and its time slice */
	if ((err = sys_schedctl(0, rmp->endpoint, 0, 0)) != OK) {
		printf("Sched: Error taking over scheduling for %d, kernel said %d\n",
			rmp->endpoint, err);
		return err;
	}
	rmp->flags = IN_USE;

	/* Schedule the process, giving it some quantum */
	if ((err = schedule_process(rmp, "do_start_scheduling")) != OK) {
		printf("Sched: Error while scheduling process, kernel replied %d\n",
			err);
		return err;
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
	int err;
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

	printf("DOING NICE\n");
	rmp = &schedproc[proc_nr_n];

	printf("MAXPRIO:%d\n", m_ptr->SCHEDULING_MAXPRIO);
	new_num_tix = rmp->num_tix + m_ptr->SCHEDULING_MAXPRIO;
	
	do_print_user_queues("do_nice");

	/* Store old values, in case we need to roll back the changes */
	old_num_tix = rmp->num_tix;

	/* Update the proc entry and reschedule the process */
	rmp->num_tix = new_num_tix;

	if ((err = schedule_process(rmp, "do_nice")) != OK) {
		/* Something went wrong when rescheduling the process, roll
		 * back the changes to proc struct */
		rmp->num_tix = old_num_tix;
	}

	return err;
}

/*===========================================================================*
 *				schedule_process			     *
 *===========================================================================*/
PRIVATE int schedule_process(struct schedproc *rmp, char *tag)
{
	int err; 

/*	printf("CMPS111 SCHEDULE PROCESS\n");*/
	do_print_process(rmp, tag);
	if ((err = sys_schedule(rmp->endpoint, rmp->priority,
			rmp->time_slice)) != OK) {
		printf("SCHED: An error occurred when trying to schedule %d: %d\n",
		rmp->endpoint, err);
	}

	return err;
}


/*===========================================================================*
 *				start_scheduling			     *
 *===========================================================================*/

PUBLIC void init_scheduling(void)
{
	NR_TICKETS = 0;
	/*srand(time(NULL));*/
}

PRIVATE int do_changetickets(struct schedproc *rmp, int val)
{
	/*TODO? add print/return for errors?*/
	if (val <= 0) {
		return 1;
	}
	if ( (rmp->num_tix + val) > MIN_USER_Q ) {
		NR_TICKETS += val;
		rmp->num_tix += val;
		/*rmp->max_tix += val;*/
	}
	return OK;
}

/*=============================================================================*
 *                                do_lottery                                   *
 *=============================================================================*/
PRIVATE int do_lottery(void)
{
	struct schedproc *rmp;
	int proc_nr;
	int numTixTot;
	int winner;
	int err;

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
	if(LOTTERY_PRINT)
		printf("%s::numTixTot:%4d winner:%4d\n", "do_lottery", numTixTot, winner);
	
	/* determine owner of winning ticket */
	for (proc_nr=0, rmp=schedproc; proc_nr < NR_PROCS; proc_nr++, rmp++) {
		if (rmp->flags & IN_USE && rmp->num_tix > 0) {
			if (rmp->num_tix < winner) {
				winner -= rmp->num_tix;
			} else { 
				do_print_process(rmp, "found_winner");
				break;/* found the winner! */
			}
		}
	}

	/* schedule the lottery winner */
	rmp->priority = WINNER_PR;
	rmp->num_tix--;
	rmp->time_slice = USER_QUANTUM;
	if ((err = schedule_process(rmp, "do_lottery")) != OK) {
		printf("Sched: Error while scheduling process, kernel replied %d\n", err);
			return err;
	}

	return OK;
}
