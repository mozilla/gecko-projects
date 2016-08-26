/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <kernel/OS.h>

#include "primpl.h"

/*
** Create a new semaphore object.
*/
PR_IMPLEMENT(PRSemaphore*)
    PR_NewSem (PRUintn value)
{
	PRSemaphore *semaphore;

	if (!_pr_initialized) _PR_ImplicitInitialization();

	semaphore = PR_NEWZAP(PRSemaphore);
	if (NULL != semaphore) {
		if ((semaphore->sem = create_sem(value, "nspr_sem")) < B_NO_ERROR)
			return NULL;
		else 
			return semaphore;
	}
	return NULL;
}

/*
** Destroy the given semaphore object.
**
*/
PR_IMPLEMENT(void)
    PR_DestroySem (PRSemaphore *sem)
{
	status_t result;

	PR_ASSERT(sem != NULL);
	result = delete_sem(sem->sem);
	PR_ASSERT(result == B_NO_ERROR);
	PR_DELETE(sem);
} 

/*
** Wait on a Semaphore.
** 
** This routine allows a calling thread to wait or proceed depending upon
** the state of the semahore sem. The thread can proceed only if the
** counter value of the semaphore sem is currently greater than 0. If the
** value of semaphore sem is positive, it is decremented by one and the
** routine returns immediately allowing the calling thread to continue. If
** the value of semaphore sem is 0, the calling thread blocks awaiting the
** semaphore to be released by another thread.
** 
** This routine can return PR_PENDING_INTERRUPT if the waiting thread 
** has been interrupted.
*/
PR_IMPLEMENT(PRStatus)
    PR_WaitSem (PRSemaphore *sem)
{
	PR_ASSERT(sem != NULL);
	if (acquire_sem(sem->sem) == B_NO_ERROR)
		return PR_SUCCESS;
	else
		return PR_FAILURE;
}

/*
** This routine increments the counter value of the semaphore. If other
** threads are blocked for the semaphore, then the scheduler will
** determine which ONE thread will be unblocked.
*/
PR_IMPLEMENT(void)
    PR_PostSem (PRSemaphore *sem)
{
	status_t result;

	PR_ASSERT(sem != NULL);
	result = release_sem_etc(sem->sem, 1, B_DO_NOT_RESCHEDULE);
	PR_ASSERT(result == B_NO_ERROR);
}

/*
** Returns the value of the semaphore referenced by sem without affecting
** the state of the semaphore.  The value represents the semaphore value
** at the time of the call, but may not be the actual value when the
** caller inspects it.
*/
PR_IMPLEMENT(PRUintn)
    PR_GetValueSem (PRSemaphore *sem)
{
	sem_info	info;

	PR_ASSERT(sem != NULL);
	get_sem_info(sem->sem, &info);
	return info.count;
}
