/* This file contains some utility routines for SCHED.
 *
 * The entry points are:
 *   no_sys:		called for invalid system call numbers
 *   sched_isokendpt:	check the validity of an endpoint
 *   sched_isemtyendpt  check for validity and availability of endpoint slot
 *   accept_message	check whether message is allowed
 */

#include "sched.h"
#include <machine/archtypes.h>
#include <sys/resource.h> /* for PRIO_MAX & PRIO_MIN */
#include "kernel/proc.h" /* for queue constants */
#include "schedproc.h"

PUBLIC void do_print_process(struct schedproc *temp_rmp, char* tag, int LOTTERY_PRINT)
{
	if (!LOTTERY_PRINT) {
		return;
	}
	if (!(temp_rmp->flags & IN_USE)) {
		return;
	}
	printf("%s:pid?:%3d endpt:%5d pri:%2d tix:%3d q:%3d\n", 
		tag, _ENDPOINT_P(temp_rmp->endpoint), temp_rmp->endpoint, 
		temp_rmp->priority, temp_rmp->num_tix, temp_rmp->time_slice);
}

PUBLIC void do_print_user_queues(char *tag, int LOTTERY_PRINT)
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
			do_print_process(loop_rmp, tag, LOTTERY_PRINT);
		}
	}
}

PUBLIC struct schedproc *get_winner(int LOTTERY_PRINT)
{
	struct schedproc *rmp;
	int proc_nr;

	/*find and return process in WINNER_PR*/
	for (proc_nr=0, rmp=schedproc; proc_nr < NR_PROCS; proc_nr++, rmp++) {
		if (rmp->flags & IN_USE && rmp->num_tix > 0) {
			if (rmp->priority == MAX_USER_Q) { 
				do_print_process(rmp, "get_winner", LOTTERY_PRINT);
				break;/* found the winner! */
			}
		}
	}

	return rmp;
}

/*===========================================================================*
 *				no_sys					     *
 *===========================================================================*/
PUBLIC int no_sys(int who_e, int call_nr)
{
/* A system call number not implemented by PM has been requested. */
  printf("SCHED: in no_sys, call nr %d from %d\n", call_nr, who_e);
  return(ENOSYS);
}


/*===========================================================================*
 *				sched_isokendpt			 	     *
 *===========================================================================*/
PUBLIC int sched_isokendpt(int endpoint, int *proc)
{
	*proc = _ENDPOINT_P(endpoint);
	if (*proc < 0)
		return (EBADEPT); /* Don't schedule tasks */
	if(*proc >= NR_PROCS)
		return (EINVAL);
	if(endpoint != schedproc[*proc].endpoint)
		return (EDEADEPT);
	if(!(schedproc[*proc].flags & IN_USE))
		return (EDEADEPT);
	return (OK);
}

/*===========================================================================*
 *				sched_isemtyendpt		 	     *
 *===========================================================================*/
PUBLIC int sched_isemtyendpt(int endpoint, int *proc)
{
	*proc = _ENDPOINT_P(endpoint);
	if (*proc < 0)
		return (EBADEPT); /* Don't schedule tasks */
	if(*proc >= NR_PROCS)
		return (EINVAL);
	if(schedproc[*proc].flags & IN_USE)
		return (EDEADEPT);
	return (OK);
}

/*===========================================================================*
 *				accept_message				     *
 *===========================================================================*/
PUBLIC int accept_message(message *m_ptr)
{
	/* accept all messages from PM and RS */
	switch (m_ptr->m_source) {

		case PM_PROC_NR:
		case RS_PROC_NR:
			return 1;
			
	}
	
	/* no other messages are allowable */
	return 0;
}
