/* This file contains the scheduling policy for SCHED
 * CS_CPU: e:36685 p:127 
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
#include "kernel/proc.h"
 /* for queue constants */

PRIVATE timer_t sched_timer;
PRIVATE unsigned balance_timeout;

PRIVATE int DEBUG;

#define NR_TIX_DEFAULT 20 /*todo should also be default max_tix, unless do_nice*/
#define MIN_NR_TIX 1

FORWARD _PROTOTYPE( int schedule_process, (struct schedproc * rmp, char *tag) );
/*FORWARD _PROTOTYPE( void balance_queues, (struct timer *tp)		);*/
PRIVATE int do_lottery(void);

/*============================================================================================*
##    ##  #######           #######  ##     ##    ###    ##    ## ######## ##     ## ##     ## 
###   ## ##     ##         ##     ## ##     ##   ## ##   ###   ##    ##    ##     ## ###   ### 
####  ## ##     ##         ##     ## ##     ##  ##   ##  ####  ##    ##    ##     ## #### #### 
## ## ## ##     ##         ##     ## ##     ## ##     ## ## ## ##    ##    ##     ## ## ### ## 
##  #### ##     ##         ##  ## ## ##     ## ######### ##  ####    ##    ##     ## ##     ## 
##   ### ##     ##         ##    ##  ##     ## ##     ## ##   ###    ##    ##     ## ##     ## 
##    ##  #######  #######  ##### ##  #######  ##     ## ##    ##    ##     #######  ##     ## 
 *============================================================================================*/

PUBLIC int do_noquantum(message *m_ptr)
{
	register struct schedproc *rmp;
	int err, proc_nr_n;
	
	printf("%s::%s\n", "CMPS111", "do_noquantum");

	if (sched_isokendpt(m_ptr->m_source, &proc_nr_n) != OK) {
		printf("do_noquantum: WARNING: got an invalid endpoint in OOQ msg %u.\n",
		m_ptr->m_source);
		return EBADEPT;
	}

	rmp = &schedproc[proc_nr_n];
	if (rmp->IS_SYS) {
		if ((err = schedule_process(rmp, "sys_noquantum")) != OK) {
			return err;
		}
		return OK;
	}
	do_print_process(rmp, "not_sys", DEBUG);
	if (rmp->priority == MAX_USER_Q) {
		rmp->priority = MIN_USER_Q;
		rmp->time_slice = -1;
		do_print_process(rmp, "do_noquantum_winner", DEBUG);
		if ((err = schedule_process(rmp, "do_noquantum")) != OK) {
			return err;
		}
	} else if (rmp->priority == MIN_USER_Q) {
		do_print_process(rmp, "do_noquantum_loser", DEBUG);
		do_lottery();
		return OK;
	}
	/*update our proctable@rmp@time_slice to 0?*/
	return OK;
}

/*==========================================================================================*
 ######  ########  #######  ########           ######   ######  ##     ## ######## ########  
##    ##    ##    ##     ## ##     ##         ##    ## ##    ## ##     ## ##       ##     ## 
##          ##    ##     ## ##     ##         ##       ##       ##     ## ##       ##     ## 
 ######     ##    ##     ## ########           ######  ##       ######### ######   ##     ## 
      ##    ##    ##     ## ##                      ## ##       ##     ## ##       ##     ## 
##    ##    ##    ##     ## ##                ##    ## ##    ## ##     ## ##       ##     ##
 ######     ##     #######  ##        #######  ######   ######  ##     ## ######## ########  
 *==========================================================================================*/
PUBLIC int do_stop_scheduling(message *m_ptr)
{
	register struct schedproc *rmp;
	int err, proc_nr_n;

	do_print("CMPS111", "do_stop_scheduling");

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	if (sched_isokendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n) != OK) {
		printf("do_stop_scheduling: WARNING: got an invalid endpoint in OOQ msg "
				"%ld\n", m_ptr->SCHEDULING_ENDPOINT);
		return EBADEPT;
	}

	rmp = &schedproc[proc_nr_n];
	do_print_process(rmp, "do_stop_scheduling", DEBUG);
	rmp->flags &= ~IN_USE;
	/*if ((err = schedule_process(rmp, "do_stop_scheduling")) != OK) {
		printf("ERROROROAROAROROR\n");
	}*/
	if (rmp->priority == MAX_USER_Q) {
		do_lottery();
	}
	return OK;
}

/*===================================================================================================*
 ######  ########    ###    ########  ########          ######   ######  ##     ## ######## ######## 
##    ##    ##      ## ##   ##     ##    ##            ##    ## ##    ## ##     ## ##       ##     ##
##          ##     ##   ##  ##     ##    ##            ##       ##       ##     ## ##       ##     ##
 ######     ##    ##     ## ########     ##             ######  ##       ######### ######   ##     ##
      ##    ##    ######### ##   ##      ##                  ## ##       ##     ## ##       ##     ##
##    ##    ##    ##     ## ##    ##     ##            ##    ## ##    ## ##     ## ##       ##     ##
 ######     ##    ##     ## ##     ##    ##    #######  ######   ######  ##     ## ######## ######## 
 *===================================================================================================*/
PUBLIC int do_start_scheduling(message *m_ptr)
{
	register struct schedproc *rmp;
	int err, proc_nr_n, parent_nr_n, nice;
	char *tag;
	
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
	rmp->endpoint 			= m_ptr->SCHEDULING_ENDPOINT;
	rmp->parent 			= m_ptr->SCHEDULING_PARENT;
	rmp->max_priority 		= (unsigned) m_ptr->SCHEDULING_MAXPRIO;
	if (rmp->max_priority >= NR_SCHED_QUEUES) {
		return EINVAL;
	}
	
	switch (m_ptr->m_type) {

		case SCHEDULING_START:
			/* We have a special case here for system processes, for which
			 * quantum and priority are set explicitly rather than inherited 
			 * from the parent */
			rmp->priority   = rmp->max_priority;/*TODO? try MAX_USER_Q*/
			rmp->time_slice = (unsigned) m_ptr->SCHEDULING_QUANTUM;
			rmp->num_tix    = (unsigned) 0;
			rmp->IS_SYS     = 1;

			tag = "SCHEDULING_START";
			break;
			
		case SCHEDULING_INHERIT:
			/* Inherit current priority and time slice from parent. Since there
			 * is currently only one scheduler scheduling the whole system, this
			 * value is local and we assert that the parent endpoint is valid */
			if ((err = sched_isokendpt(m_ptr->SCHEDULING_PARENT,
					&parent_nr_n)) != OK)
				return err;

			rmp->priority   = MIN_USER_Q;
			rmp->time_slice = -1;
			rmp->num_tix    = (unsigned) NR_TIX_DEFAULT;
			rmp->IS_SYS     = 0;

			tag = "SCHEDULING_INHERIT";
			break;
			
		default: 
			/* not reachable */
			assert(0);
	}

	do_print_process(rmp, tag, DEBUG);

	/* Take over scheduling the process. The kernel reply message populates
	 * the processes current priority and its time slice */
	if ((err = sys_schedctl(0, rmp->endpoint, 0, 0)) != OK) {
		printf("do_start_scheduling: Error taking over scheduling for %d, kernel said %d\n",
			rmp->endpoint, err);
		return err;
	}
	rmp->flags = IN_USE;

	/* Schedule the process, giving it some quantum */
	if ((err = schedule_process(rmp, tag)) != OK) {
		/*printf("do_start_scheduling: Error while scheduling process, kernel replied %d\n",
			err);*/
		return err;
	}

	/* Mark ourselves as the new scheduler.
	 * By default, processes are scheduled by the parents scheduler. In case
	 * this scheduler would want to delegate scheduling to another
	 * scheduler, it could do so and then write the endpoint of that
	 * scheduler into SCHEDULING_SCHEDULER
	 */
	m_ptr->SCHEDULING_SCHEDULER = SCHED_PROC_NR;

	/*do_lottery if not sys*/
	if (!rmp->IS_SYS) {
		do_lottery();
	}

	return OK;
}

/*=========================================================*
########   #######          ##    ## ####  ######  ######## 
##     ## ##     ##         ###   ##  ##  ##    ## ##       
##     ## ##     ##         ####  ##  ##  ##       ##       
##     ## ##     ##         ## ## ##  ##  ##       ######   
##     ## ##     ##         ##  ####  ##  ##       ##       
##     ## ##     ##         ##   ###  ##  ##    ## ##       
########   #######  ####### ##    ## ####  ######  ######## 
 *=========================================================*/
PUBLIC int do_nice(message *m_ptr)
{
	struct schedproc *rmp;
	int err; int nice;
	int proc_nr_n, proc_nr;

	/* check who can send you requests */
	if (!accept_message(m_ptr))
		return EPERM;

	if (sched_isokendpt(m_ptr->SCHEDULING_ENDPOINT, &proc_nr_n) != OK) {
		printf("do_nice: WARNING: got an invalid endpoint in OOQ msg "
		"%ld\n", m_ptr->SCHEDULING_ENDPOINT);
		return EBADEPT;
	}

	rmp = &schedproc[proc_nr_n];
	nice = m_ptr->SCHEDULING_MAXPRIO;

	if (nice == 0) {
		DEBUG = !DEBUG;/*for debugging*/
		printf("DEBUG:%d\n", DEBUG);
	} else if (nice == 1) {
		do_print_user_queues("do_nice", DEBUG);
	} else if (nice == 2) {
		do_print_queue_info("do_nice", DEBUG);
	}

	/* Update the proc entry and reschedule the process */
	rmp->num_tix += m_ptr->SCHEDULING_MAXPRIO;
	if ((err = schedule_process(rmp, "do_nice")) != OK) {
		/* Something went wrong when rescheduling the process, roll
		 * back the changes to proc struct */
		rmp->num_tix -= m_ptr->SCHEDULING_MAXPRIO;
		return err;
	}

	return OK;
}

/*==========================================================================================*
 ######   ######  ##     ## ######## ########        ########  ########   #######   ######  
##    ## ##    ## ##     ## ##       ##     ##       ##     ## ##     ## ##     ## ##    ## 
##       ##       ##     ## ##       ##     ##       ##     ## ##     ## ##     ## ##       
 ######  ##       ######### ######   ##     ##       ########  ########  ##     ## ##       
      ## ##       ##     ## ##       ##     ##       ##        ##   ##   ##     ## ##       
##    ## ##    ## ##     ## ##       ##     ##       ##        ##    ##  ##     ## ##    ## 
 ######   ######  ##     ## ######## ########  ##### ##        ##     ##  #######   ######  
 *==========================================================================================*/
PRIVATE int schedule_process(struct schedproc *rmp, char *tag)
{
	int err; char buf[100];

	memset(buf, (char)0, 100*sizeof(char));
	if (tag != NULL) {
		strcat(buf, "schedule-");
		strcat(buf, tag);
	} else {
		strcat(buf, "schedule::");
	}
	do_print_process(rmp, buf, DEBUG);
	if ((err = sys_schedule(rmp->endpoint, rmp->priority,
			rmp->time_slice)) != OK) {
		printf("%s: An error occurred when trying to schedule %d: %d\n",
				buf, rmp->endpoint, err);
	}

	return err;
}


/*================================================================================*
#### ##    ## #### ########          ######   ######  ##     ## ######## ######## 
 ##  ###   ##  ##     ##            ##    ## ##    ## ##     ## ##       ##     ##
 ##  ####  ##  ##     ##            ##       ##       ##     ## ##       ##     ##
 ##  ## ## ##  ##     ##             ######  ##       ######### ######   ##     ##
 ##  ##  ####  ##     ##                  ## ##       ##     ## ##       ##     ##
 ##  ##   ###  ##     ##            ##    ## ##    ## ##     ## ##       ##     ##
#### ##    ## ####    ##    #######  ######   ######  ##     ## ######## ######## 
 *================================================================================*/
PUBLIC void init_scheduling(void)
{
	DEBUG = 1;
	/*srand(time(NULL)); causes deadlock!?*/
}

/*==========================================================================================*
########   #######          ##        #######  ######## ######## ######## ########  ##    ## 
##     ## ##     ##         ##       ##     ##    ##       ##    ##       ##     ##  ##  ##  
##     ## ##     ##         ##       ##     ##    ##       ##    ##       ##     ##   ####   
##     ## ##     ##         ##       ##     ##    ##       ##    ######   ########     ##    
##     ## ##     ##         ##       ##     ##    ##       ##    ##       ##   ##      ##    
##     ## ##     ##         ##       ##     ##    ##       ##    ##       ##    ##     ##    
########   #######  ####### ########  #######     ##       ##    ######## ##     ##    ##   
 *==========================================================================================*/
PRIVATE int do_lottery(void)
{
	struct schedproc *rmp;
	int proc_nr;
	int winner;
	int err;

	int NR_TICKETS = 0;

	printf("%s::%s\n", "CMPS111", "do_lottery");

	/* pull down all winners (hack?) */
	for (proc_nr=0, rmp=schedproc; proc_nr < NR_PROCS; proc_nr++, rmp++) {
		if ((rmp->flags & IN_USE) && (!rmp->IS_SYS) 
				&& (rmp->priority >= MAX_USER_Q) 
				&& (rmp->priority <= MIN_USER_Q)) {
			if (rmp->priority == MAX_USER_Q) {
				do_print_process(rmp, "shouldnt-be", DEBUG);
				rmp->priority = MIN_USER_Q;/*lower its priority*/
				rmp->time_slice = (unsigned) -1;
				if ((err = schedule_process(rmp, "loseifying")) != OK) {}
			}
		}
	}

	/* sum number of tickets in each process */
	for (proc_nr=0, rmp=schedproc; proc_nr < NR_PROCS; proc_nr++, rmp++) {
		if ((rmp->flags & IN_USE) && (!rmp->IS_SYS) 
				&& (rmp->priority >= MAX_USER_Q) 
				&& (rmp->priority <= MIN_USER_Q)) {
			if (rmp->priority == MIN_USER_Q) {/*aka: a loser*/
				/*do_print_process(rmp, "addin_tix", DEBUG);*/
				NR_TICKETS += rmp->num_tix;
			}
		}
	}
	
	/* pick a winning ticket */
	winner = rand() % NR_TICKETS;
	if (DEBUG)
		printf("%s::NR_TICKETS:%4d winner:%4d\n", "do_lottery", NR_TICKETS, winner);
	
	/* determine owner of winning ticket */
	for (proc_nr=0, rmp=schedproc; proc_nr < NR_PROCS; proc_nr++, rmp++) {
		if (rmp->flags & IN_USE && rmp->num_tix > 0 && (!rmp->IS_SYS)
				&& rmp->priority == MIN_USER_Q) {
			if (rmp->num_tix < winner) {
				winner -= rmp->num_tix;
			} else { 
				do_print_process(rmp, "found_winner", DEBUG);
				break;/* found the winner! */
			}
		}
	}

	/* upgrade and schedule the lottery winner */
	rmp->priority = MAX_USER_Q;
	rmp->time_slice = USER_QUANTUM;
	if ((err = schedule_process(rmp, "scheduling_winner")) != OK) {
		printf("PANIC: do_lottery scheduling failed, kernel replied %d\n", err);
		panic("do_lottery failed, err:%s", err);
	}

	do_print_queue_info("do_lottery", DEBUG);

	return OK;
}
