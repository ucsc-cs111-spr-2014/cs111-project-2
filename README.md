cs111-os-project-2
==================
BASIC DESIGN OVERVIEW-----------------------------------------------------------------------
+ We want to use queues 14 and 15 for our lottery scheduler.
+ As a process is created, it is assigned 20 tickets and placed in queue 15.
+ The currently running process is is queue 14.
+ When a process blocks or runs out of quantum we reassign it to queue 15 and draw a new ticket

WHAT WE HAVE DONE---------------------------------------------------------------------------
+ cam: added unsigned variable num_tix to the schedproc struct in schedprod.h to keep a count of the number of tickets each process has.
+ cam: defined constants DEFAULT_SCHED and LOTTERY_SCHED and surrounded code that we will change with if statements so that we can test both ways easily later on.  For right now I surrounded every call to schedule_process() with the check for which scheduler.  Im not sure if we want to keep it this way OR have schedule_process() be called either way and we will have different code for the different schedulers within there.

WHAT WE SHOULD DO NEXT----------------------------------------------------------------------
+ create a function to hold a lottery drawing.  The function will be called after a do a do_noquantum() or do_stop_scheduling() call happens.  It will find the total number of tickets, then randomly generate an int between 0 and total tickets - 1.  It will use this number to pick the winning process(although Im not sure how right now)
