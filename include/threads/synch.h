#ifndef THREADS_SYNCH_H
#define THREADS_SYNCH_H

#include <list.h>
#include <stdbool.h>

/* A counting semaphore. */
struct semaphore {
	unsigned value;             /* Current value. */
	
	// 이 waiters에는 이 semaphore에 관련하여 잠자고 있는 스레드 (struct thread의 elem 멤버)이 저장됨
	struct list waiters;        /* List of waiting threads. */
	
};
/* Lock. */
struct lock {
	struct thread *holder;      /* Thread holding lock (for debugging). */
	struct semaphore semaphore; /* Binary semaphore controlling access. */
};

/* Condition variable. */
// 각 공유 자원마다 하나씩 가짐. 공유 자원별로 따로따로 하나씩 갖고 있어야 함.
struct condition {
	// 이 waiters에는 조건이 충족될 때까지 기다리는 세마포어들이 저장됨.
	struct list waiters;        /* List of waiting threads. */
};

/* 참고용: synch.c의 semaphore_elem
// 현재 스레드가 사용할 "자기 전용 이진 세마포어".
struct semaphore_elem {
	// 참고: elem의 멤버는 struct list_elem *prev, *next;
	struct list_elem elem;      
	struct semaphore semaphore; 
};
*/

void sema_init (struct semaphore *, unsigned value);
void sema_down (struct semaphore *);
bool sema_try_down (struct semaphore *);
void sema_up (struct semaphore *);
void sema_self_test (void);

void lock_init (struct lock *);
void lock_acquire (struct lock *);
bool lock_try_acquire (struct lock *);
void lock_release (struct lock *);
bool lock_held_by_current_thread (const struct lock *);

void cond_init (struct condition *);
void cond_wait (struct condition *, struct lock *);
void cond_signal (struct condition *, struct lock *);
void cond_broadcast (struct condition *, struct lock *);

/*-- Priority condvar 구현 --*/
bool sema_priority_cmp(const struct list_elem *a, const struct list_elem *b, void *aux );
bool donation_priority_cmp(const struct list_elem *a,
						   const struct list_elem *b, void *aux);
/*-- Priority condvar 구현 --*/

/* Optimization barrier.
 *
 * The compiler will not reorder operations across an
 * optimization barrier.  See "Optimization Barriers" in the
 * reference guide for more information.*/
#define barrier() asm volatile ("" : : : "memory")

#endif /* threads/synch.h */
