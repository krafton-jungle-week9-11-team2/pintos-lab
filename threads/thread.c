#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static void do_schedule(int status);
static void schedule (void);
static tid_t allocate_tid (void);

/* Returns true if T appears to point to a valid thread. */
#define is_thread(t) ((t) != NULL && (t)->magic == THREAD_MAGIC)

/* Returns the running thread.
 * Read the CPU's stack pointer `rsp', and then round that
 * down to the start of a page.  Since `struct thread' is
 * always at the beginning of a page and the stack pointer is
 * somewhere in the middle, this locates the curent thread. */
#define running_thread() ((struct thread *) (pg_round_down (rrsp ())))


// Global descriptor table for the thread_start.
// Because the gdt will be setup after the thread_init, we should
// setup temporal gdt first.
static uint64_t gdt[3] = { 0, 0x00af9a000000ffff, 0x00cf92000000ffff };

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) {
	ASSERT (intr_get_level () == INTR_OFF);

	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds);

	/* Init the globla thread context */
	lock_init (&tid_lock);
	list_init (&ready_list);
	list_init (&destruction_req);

	/* Set up a thread structure for the running thread. */
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) {
	/* Create the idle thread. */
	struct semaphore idle_started;
	sema_init (&idle_started, 0);
	thread_create ("idle", PRI_MIN, idle, &idle_started);

	/* Start preemptive thread scheduling. */
	intr_enable ();

	/* Wait for the idle thread to initialize idle_thread. */
	sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) {
	struct thread *t = thread_current ();
  // cpu를 쓰고 있는 스레드를 변수 t에 저장 
	/* Update statistics. */
	if (t == idle_thread)
	//만약 지금 도는 스레드가 idle thread라면, → "쉬는 중"
	//dle thread는 아무 일도 안 하고 “누구 실행시킬 애 없나~” 기다리는 애야.
		idle_ticks++;
		//쉬는 시간(idle_ticks)을 1 증가시켜!
#ifdef USERPROG
//만약 이 스레드가 유저 프로그램이라면 user_ticks++
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
	// 그게 아니라면 커널 스레드니까 커널에 ++ 
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
	//thread_ticks는 이 스레드가 연속으로 몇 틱 동안 실행됐는지 세는 변수
	//++thread_ticks → 한 틱 지났으니 숫자 1 올려.
		intr_yield_on_return ();
		//지금 바로 context switch는 안 하고,
		// 인터럽트 핸들러가 끝나고 돌아갈 때 CPU 양보하겠다는 뜻이야.
		//, 지금은 인터럽트 핸들러 안이니까
		// → 직접 thread_yield() 해버리면 위험할 수 있음! -> 현재 스레드 상태 저장도 안햇는데 문맥 전환 일어나면 안됨.
		// ➡ 그래서 “돌아가면서 자연스럽게 양보하자~” 라고 하는 거야.


}

/* Prints thread statistics. */
void
thread_print_stats (void) {
	printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
			idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
		thread_func *function, void *aux) {
	struct thread *t;
	tid_t tid;

	ASSERT (function != NULL);

	/* Allocate thread. */
	t = palloc_get_page (PAL_ZERO);
	if (t == NULL)
		return TID_ERROR;

	/* Initialize thread. */
	init_thread (t, name, priority);
	tid = t->tid = allocate_tid ();

	/* Call the kernel_thread if it scheduled.
	 * Note) rdi is 1st argument, and rsi is 2nd argument. */
	t->tf.rip = (uintptr_t) kernel_thread;
	t->tf.R.rdi = (uint64_t) function;
	t->tf.R.rsi = (uint64_t) aux;
	t->tf.ds = SEL_KDSEG;
	t->tf.es = SEL_KDSEG;
	t->tf.ss = SEL_KDSEG;
	t->tf.cs = SEL_KCSEG;
	t->tf.eflags = FLAG_IF;

	/* Add to run queue. */
	thread_unblock (t);

	return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) {
	ASSERT (!intr_context ());
	ASSERT (intr_get_level () == INTR_OFF);
	thread_current ()->status = THREAD_BLOCKED;
	schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) {
	enum intr_level old_level;

	ASSERT (is_thread (t));

	old_level = intr_disable ();
	ASSERT (t->status == THREAD_BLOCKED);
	list_push_back (&ready_list, &t->elem);
	t->status = THREAD_READY;
	intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) {
	return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
	//  현재 실행 중인 스레드(struct thread 구조체)를 반환하는 함수
struct thread *
thread_current (void) {
	struct thread *t = running_thread ();

	/* Make sure T is really a thread.
	   If either of these assertions fire, then your thread may
	   have overflowed its stack.  Each thread has less than 4 kB
	   of stack, so a few big automatic arrays or moderate
	   recursion can cause stack overflow. */
	ASSERT (is_thread (t));
	ASSERT (t->status == THREAD_RUNNING);

	return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) {
	return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
 // 현재 실행 중인 스레드가 스스로 종료할 때 호출되는 함수
void
thread_exit (void) {
	ASSERT (!intr_context ());
//이 함수는 인터럽트 핸들러 안에서는 호출되면 안 됨!!
#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	//문맥 전환 전에 항상 인터럽트 잠깐 끔


	do_schedule (THREAD_DYING);
	//지금 스레드 상태를 THREAD_DYING 으로 설정하고
// 스케줄러 불러서 다음 스레드로 context switch 시도
	NOT_REACHED ();
	// 1. 현재 스레드가 자기 죽을 차례라고 선언
// 2. 사용자 프로세스도 종료해주고
// 3. 인터럽트 끔
// 4. 상태를 THREAD_DYING으로 설정
// 5. 스케줄러 호출 → 다음 스레드로 문맥 전환
// 6. 이후 메모리 정리는 다음 스레드가 해줌!

}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
	 //스스로 CPU 양보하겠다고 요청하는 스레드 함수
void
thread_yield (void) {
	struct thread *curr = thread_current ();
	//현재 실행 중인 스레드를 curr에 저장함
	//thread_current()는 지금 CPU에서 돌고 있는 스레드 정보를 반환해
// 즉, “나 자신”을 curr로 참조해둔 거야
	enum intr_level old_level;
	//  인터럽트 상태를 저장할 변수 선언
	ASSERT (!intr_context ());
// 디버깅용 체크!// "지금 인터럽트 안에서 이 함수 부르면 안 됨.ㅣ -> 인터럽트 핸들러 안에서 문맥 전환 안되잖아 그거 막는거임.
	old_level = intr_disable ();
	//old_level = intr_disable();
	// 스레드를 ready_list에 넣는 동안 다른 인터럽트가 끼어들면 꼬일 수 있어. 그래서 잠깐 끔.
	if (curr != idle_thread)
	//idle_thread(할 일 없는 대기용 스레드)는 ready_list에 넣지 않음
		list_push_back (&ready_list, &curr->elem);
		//현재 스레드를 ready_list에 다시 넣음 (큐 뒤에 추가)

	// 문맥 전환을 하는 함수 -> 여기서 다른 스레드로 교체 가능 
	do_schedule (THREAD_READY);
	//THREAD_READY => 나 CPU 양보할게. 다시 ready 상태로 가서 대기할게
	//THREAD_BLOCKED => 나는 I/O 기다리거나, 조건 기다려야 돼. 실행 X
	//THREAD_DYING	 => 나 종료할게. 곧 없어짐

	intr_set_level (old_level);
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) {
	thread_current ()->priority = new_priority;
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) {
	return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) {
	/* TODO: Your implementation goes here */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) {
	/* TODO: Your implementation goes here */
	return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) {
	struct semaphore *idle_started = idle_started_;

	idle_thread = thread_current ();
	sema_up (idle_started);

	for (;;) {
		/* Let someone else run. */
		intr_disable ();
		thread_block ();

		/* Re-enable interrupts and wait for the next one.

		   The `sti' instruction disables interrupts until the
		   completion of the next instruction, so these two
		   instructions are executed atomically.  This atomicity is
		   important; otherwise, an interrupt could be handled
		   between re-enabling interrupts and waiting for the next
		   one to occur, wasting as much as one clock tick worth of
		   time.

		   See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
		   7.11.1 "HLT Instruction". */
		asm volatile ("sti; hlt" : : : "memory");
	}
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) {
	ASSERT (function != NULL);

	intr_enable ();       /* The scheduler runs with interrupts off. */
	function (aux);       /* Execute the thread function. */
	thread_exit ();       /* If function() returns, kill the thread. */
}


/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority) {
	ASSERT (t != NULL);
	ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
	ASSERT (name != NULL);

	memset (t, 0, sizeof *t);
	t->status = THREAD_BLOCKED;
	strlcpy (t->name, name, sizeof t->name);
	t->tf.rsp = (uint64_t) t + PGSIZE - sizeof (void *);
	t->priority = priority;
	t->magic = THREAD_MAGIC;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) {
	if (list_empty (&ready_list))
		return idle_thread;
	else
		return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Use iretq to launch the thread */
void
do_iret (struct intr_frame *tf) {
	__asm __volatile(
			"movq %0, %%rsp\n"
			"movq 0(%%rsp),%%r15\n"
			"movq 8(%%rsp),%%r14\n"
			"movq 16(%%rsp),%%r13\n"
			"movq 24(%%rsp),%%r12\n"
			"movq 32(%%rsp),%%r11\n"
			"movq 40(%%rsp),%%r10\n"
			"movq 48(%%rsp),%%r9\n"
			"movq 56(%%rsp),%%r8\n"
			"movq 64(%%rsp),%%rsi\n"
			"movq 72(%%rsp),%%rdi\n"
			"movq 80(%%rsp),%%rbp\n"
			"movq 88(%%rsp),%%rdx\n"
			"movq 96(%%rsp),%%rcx\n"
			"movq 104(%%rsp),%%rbx\n"
			"movq 112(%%rsp),%%rax\n"
			"addq $120,%%rsp\n"
			"movw 8(%%rsp),%%ds\n"
			"movw (%%rsp),%%es\n"
			"addq $32, %%rsp\n"
			"iretq"
			: : "g" ((uint64_t) tf) : "memory");
}

/* Switching the thread by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function. */
	 //현재 실행 중인 스레드의 상태(레지스터, 스택, RIP 등)를 저장하고,
// 다음 스레드의 상태를 복원해서 그 스레드를 실행시키는 것
static void
thread_launch (struct thread *th) {
	uint64_t tf_cur = (uint64_t) &running_thread ()->tf;
	uint64_t tf = (uint64_t) &th->tf;
	ASSERT (intr_get_level () == INTR_OFF);

	/* The main switching logic.
	 * We first restore the whole execution context into the intr_frame
	 * and then switching to the next thread by calling do_iret.
	 * Note that, we SHOULD NOT use any stack from here
	 * until switching is done. */
	__asm __volatile (
			/* Store registers that will be used. */
			"push %%rax\n"
			"push %%rbx\n"
			"push %%rcx\n"
			/* Fetch input once */
			"movq %0, %%rax\n"
			"movq %1, %%rcx\n"
			"movq %%r15, 0(%%rax)\n"
			"movq %%r14, 8(%%rax)\n"
			"movq %%r13, 16(%%rax)\n"
			"movq %%r12, 24(%%rax)\n"
			"movq %%r11, 32(%%rax)\n"
			"movq %%r10, 40(%%rax)\n"
			"movq %%r9, 48(%%rax)\n"
			"movq %%r8, 56(%%rax)\n"
			"movq %%rsi, 64(%%rax)\n"
			"movq %%rdi, 72(%%rax)\n"
			"movq %%rbp, 80(%%rax)\n"
			"movq %%rdx, 88(%%rax)\n"
			"pop %%rbx\n"              // Saved rcx
			"movq %%rbx, 96(%%rax)\n"
			"pop %%rbx\n"              // Saved rbx
			"movq %%rbx, 104(%%rax)\n"
			"pop %%rbx\n"              // Saved rax
			"movq %%rbx, 112(%%rax)\n"
			"addq $120, %%rax\n"
			"movw %%es, (%%rax)\n"
			"movw %%ds, 8(%%rax)\n"
			"addq $32, %%rax\n"
			"call __next\n"         // read the current rip.
			"__next:\n"
			"pop %%rbx\n"
			"addq $(out_iret -  __next), %%rbx\n"
			"movq %%rbx, 0(%%rax)\n" // rip
			"movw %%cs, 8(%%rax)\n"  // cs
			"pushfq\n"
			"popq %%rbx\n"
			"mov %%rbx, 16(%%rax)\n" // eflags
			"mov %%rsp, 24(%%rax)\n" // rsp
			"movw %%ss, 32(%%rax)\n"
			"mov %%rcx, %%rdi\n"
			"call do_iret\n"
			"out_iret:\n"
			: : "g"(tf_cur), "g" (tf) : "memory"
			);
}

/* Schedules a new process. At entry, interrupts must be off.
 * This function modify current thread's status to status and then
 * finds another thread to run and switches to it.
 * It's not safe to call printf() in the schedule(). */
static void
do_schedule(int status) {
	// 문맥전환 중에 인터럽트 끄기 
	ASSERT (intr_get_level () == INTR_OFF);
	
	// 지금 CPU에서 돌고 있는 스레드는 무조건 THREAD_RUNNING 상태여야 해
	ASSERT (thread_current()->status == THREAD_RUNNING);

	// 진짜 죽은 애들을 바로 free 못 하고,
// 나중에 안전한 순간에 정리하기 위해 잠깐 대기시키는 리스트!
	while (!list_empty (&destruction_req)) {
		// destruction_req는 죽기로 예약된 스레드 리스트
		// (exit() 했지만 아직 메모리 회수 안 된 애들)
		//스레드 갈아타기 전에 죽은 스레드의 메모리 정리 
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
			// destruction_req 리스트에서 죽기로 예약된 스레드를 하나 꺼낸다.
// 1. list_pop_front(): 리스트 맨 앞의 list_elem (노드) 하나 꺼냄 (list_elem* 타입)
// 2. list_entry(): 꺼낸 노드가 struct thread 안의 elem 필드였다는 걸 바탕으로,
//                 원래의 struct thread* 구조체 전체를 복원함!
//	 복원하는 이유 :리스트에는 스레드의 list_elem만 들어가지만,
// 우리가 진짜 작업하려면 → 그 전체 스레드 구조체(struct thread) 가 필요하니까, 복원해서 구조체 시작 주소를 구해야함.
//    → 즉, list_elem* → struct thread* 로 되돌려주는 마법 매크임.
//       (구조체 안에서 elem의 위치를 계산해서 시작 주소로 backtrace!)
//
// 최종 결과:
// 'victim'은 죽을 스레드를 가리키는 포인터가 되고,
// 이후 이 스레드의 메모리를 free 해서 완전히 제거할 수 있음!
//페이지 단위로 할당된 메모리를 해제하는 함수
//스레드 하나는 보통 4KB짜리 페이지 하나에 전부 들어가 있어!
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	// 현재 스레드의 상태를 바꿔줌
	// thread_current ()->status 을 인자로 받아욘 status로 바꿔줌

	schedule ();
}






//현재 스레드에서 다음 실행할 스레드로 CPU를 넘기는 진짜 문맥 전환 함수
//지금 실행 중이던 스레드에서
// 다음 실행할 스레드로 CPU를 넘겨주는 작업을 하는 함수임
static void
schedule (void) {
	struct thread *curr = running_thread ();
//`curr`: 지금 CPU에서 돌고 있는 스레드

	struct thread *next = next_thread_to_run ();
//- `next`: 다음에 CPU 줄 스레드 (ready_list에서 꺼냄!)

	ASSERT (intr_get_level () == INTR_OFF);
	//인터럽트 꺼져 있어야 함 (context switch 중에는 방해 금지!)


	ASSERT (curr->status != THREAD_RUNNING);
//현재 스레드 상태는 RUNNING이면 안 됨
// (우린 이 스레드를 곧 내보낼 거니까)

	ASSERT (is_thread (next));
	//// 다음 스레드가 진짜 스레드인지 확인 (검증)

	/* Mark us as running. */
	next->status = THREAD_RUNNING;
// 다음 스레드의 상태를 **RUNNING으로 바꿔줌**


	/* Start new time slice. */
	thread_ticks = 0;
//  새 스레드가 타이머 틱 0부터 시작하도록 초기화

#ifdef USERPROG
	/* Activate the new address space. */
	process_activate (next);
#endif

	if (curr != next) {
		/* If the thread we switched from is dying, destroy its struct
		   thread. This must happen late so that thread_exit() doesn't
		   pull out the rug under itself.
		   We just queuing the page free reqeust here because the page is
		   currently used by the stack.
		   The real destruction logic will be called at the beginning of the
		   schedule(). */
		if (curr && curr->status == THREAD_DYING && curr != initial_thread) {
			// 현재 스레드가 죽으려는 상태면 `destruction_req` 리스트에 넣어놔  -> 프리 나중에 해주기 
			ASSERT (curr != next);
			list_push_back (&destruction_req, &curr->elem);
		}

		/* Before switching the thread, we first save the information
		 * of current running. */
		thread_launch (next);
	}
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) {
	static tid_t next_tid = 1;
	tid_t tid;

	lock_acquire (&tid_lock);
	tid = next_tid++;
	lock_release (&tid_lock);

	return tid;
}
