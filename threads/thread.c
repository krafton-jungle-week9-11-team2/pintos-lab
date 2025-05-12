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
//이 파일에서 list.h 간접적으로 include
#include "threads/vaddr.h"
#include "intrinsic.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/*
======================================
      핵심 스레드 관리 코드
      struct thread 포함
이 프로젝트의 주 작업 대상 (우리가 주로 수정)
======================================

*/

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* Random value for basic thread
   Do not modify this value. */
#define THREAD_BASIC 0xd42df210

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running.
   => 대기 리스트 
*/
static struct list ready_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Thread destruction requests */
static struct list destruction_req;

/*
====================================================
   Alarm System Call 전역변수 추가

1.Thread_blocked 상태의 스레드를
 관리하기 위한 리스트 자료구조

2.sleep_list에서 대기중인 스레드들의 wakeup_tick 값 중
  최솟값을 저장하기 위한 변수 추가
  제일 최솟값을 가진 스레드들 부터 깨워줘야 하는 듯

=====================================================
*/

static struct list sleep_list;
static int64_t next_tick_to_awake;

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


/*
===================================================
             내가 새로 구현해줄 함수
    1.void thread_sleep(int64_t ticks) {}
	=> 실행중인 스레드를 재우기 => 근데 재우는거 큐?
	2.void thread_awake(int64_t ticks) {}
	=> sleep 상태인 thread를 깨우기(ready로)
	3.int64_t get_next_tick_to_awake(void){}
	=>timer_interrupt()에서 이 반환값(wakeup_tick)을
	  보고 thread_awake() 호출할지 말지를 결정
===================================================
*/

int64_t get_next_tick_to_awake(void){
	return next_tick_to_awake;

	/*
	===============================================
	timer_interrupt()에서 이 반환값(wakeup_tick)을
	  보고 thread_awake() 호출할지 말지를 결정
	===============================================
	*/
}


void thread_sleep(int64_t ticks){
/*
=======================================================================
    [전체적인 구조 파악 - 손채민의 예상편]
	1.sleep 상황 감지
    2.running => blocked (thread_block)
	3.sleep_list에 넣기 
	4.ticks 정보 저장하기 (thread ->wakeup_tick필드 활용)
	5.오름차순 저장하기 (list_insert_ordered()+비교 함수 사용하기)


	[전체적인 구조- 정답편]
	**상황감지는 다른곳에 해주는듯 그냥 이 함수는 호출되면 끝
	1.이전 인터럽트 상태 담아두기
	2.현재 실행중인 thread의 주소 가져오기
	<예외 처리 1>:만약 idle thread면 sleep에 넣어줄 필요가 없음
	3.update 함수를 통해 해당 thread의 ticks을 넣어줘서 최소틱 갱신해주기
	4.sleep queue에 넣어주기
	5.thread block 해주기 

	[궁금한 점]
	**왜 sleep_list에 먼저 넣어주고 block상태로 바꿔줘야 하는건지 
    
	=> sleep_list에 먼저 넣고 => thread_block()를 호출해야 
	   thread_awake()가 나중에 그 스레드를 찾아서 깨울 수 있음

	(if 순서를 바꿔서 thread_block()을 먼저 하면?)
	-지금 스레드는 바로 blocked상태가 된다.
	-그 상태에선 다른 함수가 이 스레드를 식별하거나, sleep_list에 접근할 수 없다.
	-즉, thread_awake()가 이 스레드를 찾지 못해서 영원히 못 깸 

	!!!!!마치 메모를 남기고 방을 나가는 느낌!!!!
	-"나 언제 깨워줘!" 라는 메모를 남겨야 
	- 다른 사람이 나를 다시 깨워줄 수 있다. (메모없이 잠들면 누가 깨워주니)
========================================================================

*/

	struct thread * curr;

	enum intr_level old_level; //열거형 
	old_level = intr_disable();  
	/*
	========================================================
	  -이 라인 이후의 과정 중에는 인터럽트를 받아들이지 않음
	  -다만 나중에 다시 받아들이기 위해 old_level에 
	   이전 인터럽트 상태를 담아둔다. 
	========================================================
	*/ 

    curr = thread_current(); // 현재 실행중인 thread의 주소 가져오기
	ASSERT(curr != idle_thread);
	/*
	==========================================================
	             ASSERT : 거짓이면 커널 멈추기
		현재의 thread가 idle thread이면 sleep 되지 않아야 한다. 
		idle이면 굳이 sleep할 필요가 없음 ! 
	==========================================================
	*/ 

    update_next_tick_to_awake(curr->wakeup_tick = ticks);
	/*
	===========================================================
	-현재의 thread의 wakeup_ticks에 인자로 들어온 
	 ticks를 저장후 next_tick_to_awake를 업데이트

	-next_tick_to_awake 함수에 넣어주면 삼항연산자로 업데이트 해줌
	===========================================================
	*/

    list_push_back(&sleep_list,&(curr->elem)); 
	/*
	=============================================================
	               list.c에 있는 함수 이용하기
		       sleep_list에 현재 thread의 element를 
		    슬립 리스트(큐)의 마지막에 삽입한 후에 스케쥴한다.
			큐는 뒤에 삽입해줘야함 , 인출은 맨 앞에서 FIFO
	=============================================================
	*/
    thread_block(); 
	/*
	=============================================================
	     현재 thread를 block 시킴,다시 스케쥴 될 때 까지
         현재 스레드를 슬립 큐에 삽입한 후에 스케줄 한다. 
		 해당 과정 중에는 인터럽트를 받아들이지 않는다. 
	=============================================================
    */
   intr_set_level(old_level); 
   /*
   ==============================================================
              다시 스케쥴하러 갈 수 있게 만들어 준다. 
	    인터럽트를 다시 받아들여서 인터럽트가 가능하도록 수정해준다. 
   ==============================================================
   */


}

void thread_awake(int64_t ticks){
/*
======================================================================
    [전체적인 구조 파악-손채민 예상ver.]
    1.이 함수를 호출하면서 ticks 인자를 전해주기
	2.sleep_list 내에서 해당 ticks의 값을 가진 스레드들 모두 깨워주기
	3.깨워준 thread들 모두 반환해주기

	[전체적인 구조 파악-정답 ver.]
	1.이 함수를 호출하면서 ticks 인자를 전해주기
	2.sleep_list 내에서 해당 ticks의 이하 값을 가진 스레드들 모두 깨워주기
	3.리스트를 앞에서부터 순회하면서 깨우는 동시에
	  => next_tick_to_awake도 다시 갱신
	  =>깨운 애는 sleep_list에서 제거되니까
	  =>남아 있는 스레드들 중 다음으로 가장 빨리 깨야 할 애의 tick을 다시 기록
	
	+ 반환할 필요는 없다.
	*thread_awake()는 깨울 애들을 깨우고
	*next_tick_to_awake만 갱신해주면 이 함수의 역할은 끝 !
=====================================================================
*/
   struct list_elem *e =list_begin(&sleep_list); 
   //sleep_list의 맨 앞을 *e라는 포인터 변수에 넣어주기
   /*
   ================================================
   e는 현재 순회중인 struct list_elem *
   ================================================
   */
   while (e!=list_end(&sleep_list)){
	//포인터 변수 e가 sleep_list의 끝 값이 아닐때 까지 루프
	struct thread *t = list_entry(e,struct thread,elem); // 구조체
	if(t->wakeup_tick<=ticks){ //ticks이하의 아이들 다 깨워주기 
		e=list_remove(e); // 깨워준 애들은 sleep_list에서 지워주기
		thread_unblock(t); //unblock해주기 !
	}
	else
	   e=list_next(e);
	   //그게 아니면 다음 스레드로 검사 넘어가기
   } 

}








/*
===================================================
 - 시스템이 처음 부팅될 때 실행되며
 - 간단히 얘기하면 초기화 해주는 함수
 - 스레드 관련 자료구조들(Ready list, Sleep list)
   초기화
 - main 스레드 자체도 하나의 thread로 등록해주는 함수
===================================================
*/

void
thread_init (void) {
	
	ASSERT (intr_get_level () == INTR_OFF);
    /*
	========================================================
    ASSERT(condition)
    => 조건이 거짓이면 커널을 즉시 중단시키는 매크로

	인터럽트가 꺼져있는지 확인
	-만약 인터럽트가 꺼져있지 않다면 
	=> 커널 패닉이 발생 ! 

	즉,개발자가 코드의 전체 조건이
	어겨지지 않았는지 체크하는 용도
	=>C언어에서 흔히 사용하는 디버깅용 매크로

	[왜 초기화 할 때 인터럽트를 꺼주는가?]
	1.중간에 끼어들면 자료가 꼬일 수 있어서
	=>OS 내부 자료 구조를 수정 중일 때
	인터럽트가 발생해서 스레드 전환이 일어나면
	아직 수정 중인 자료구조를 다른 코드가 참조 or 수정
	=> Race condition 발생! 
	=========================================================
	*/


	/* Reload the temporal gdt for the kernel
	 * This gdt does not include the user context.
	 * The kernel will rebuild the gdt with user context, in gdt_init (). */
	
	list_init(&sleep_list);   //sleep_list를 초기화
	
	/*
	=======================================
	tid(threadID)를 할당할 때 
	사용할 락 초기화
	스레드를 만들 때마다 allocate_tid()로 배정
	=> 동시성 문제 방지용 락 
	=======================================
	*/
	struct desc_ptr gdt_ds = {
		.size = sizeof (gdt) - 1,
		.address = (uint64_t) gdt
	};
	lgdt (&gdt_ds); //어셈블리 명령

	/* Init the globla thread context */
	lock_init (&tid_lock);

	list_init (&ready_list);
	list_init (&destruction_req);

	/* 
	==================================================
	Set up a thread structure for the running thread. 
	이 스레드를 정식으로 스레드 구조체로 초기화
	-이름 :main 
	-우선 순위 : PRI_DEFAULT (기본값,31)
	==================================================
	*/
	initial_thread = running_thread ();
	init_thread (initial_thread, "main", PRI_DEFAULT);
	initial_thread->status = THREAD_RUNNING;
	//main 스레드는 지금 당장 실행중 상태 running설정
	initial_thread->tid = allocate_tid ();
	//이 main 스레드에게 고유한 threadID 할당 
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

	/* Update statistics. */
	if (t == idle_thread)
		idle_ticks++;
#ifdef USERPROG
	else if (t->pml4 != NULL)
		user_ticks++;
#endif
	else
		kernel_ticks++;

	/* Enforce preemption. */
	if (++thread_ticks >= TIME_SLICE)
		intr_yield_on_return ();
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
void
thread_exit (void) {
	ASSERT (!intr_context ());

#ifdef USERPROG
	process_exit ();
#endif

	/* Just set our status to dying and schedule another process.
	   We will be destroyed during the call to schedule_tail(). */
	intr_disable ();
	do_schedule (THREAD_DYING);
	NOT_REACHED ();
}

/*
=====================================================================
 Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. 

            우선순위에 따라 CPU를 양보해주는 함수 !
	1. 현재 스레드구조체의 포인터를 얻고
    2. 인터럽트를 비활성화한 뒤
	3. 현재 스레드 구조체를 대기 리스트 끝에 배치
	4. 문맥 전환을 호출하는 방식으로 구현
=====================================================================
*/

void
thread_yield (void) {
	struct thread *curr = thread_current ();
	enum intr_level old_level;

	ASSERT (!intr_context ());

	old_level = intr_disable ();
	if (curr != idle_thread)
		list_push_back (&ready_list, &curr->elem);
	do_schedule (THREAD_READY);
	intr_set_level (old_level);
}

/*
================================================
       Priority Scheduling 관련 함수들 
	      이 함수들 구현/수정 해주기
================================================

*/
/* 
===================================================
Sets the current thread's priority to NEW_PRIORITY. 
    현재 스레드의 우선 순위를 new_priority 로 설정

 -현재 스레드의 "기본 우선순위"를 변경해준다.
 -단, 기부받은 우선순위가 있다면 => 아직 적용되지 않을수도
 -새 우선순위로 인해 더 높은 우선순위 스레드가 있다면 즉시 양보
===================================================
*/
void
thread_set_priority (int new_priority) {
	thread_current ()->priority = new_priority;
} 




/* 
===================================================
      Returns the current thread's priority. 
    - 현재 스레드의 "현재 우선순위를 반환 "
	- 만약 기부받은 우선순위가 있다면 
	  => 그 중 가장 높은 값 반환
===================================================
*/
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
	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (thread_current()->status == THREAD_RUNNING);
	while (!list_empty (&destruction_req)) {
		struct thread *victim =
			list_entry (list_pop_front (&destruction_req), struct thread, elem);
		palloc_free_page(victim);
	}
	thread_current ()->status = status;
	schedule ();
}

static void
schedule (void) {
	struct thread *curr = running_thread ();
	struct thread *next = next_thread_to_run ();

	ASSERT (intr_get_level () == INTR_OFF);
	ASSERT (curr->status != THREAD_RUNNING);
	ASSERT (is_thread (next));
	/* Mark us as running. */
	next->status = THREAD_RUNNING;

	/* Start new time slice. */
	thread_ticks = 0;

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
