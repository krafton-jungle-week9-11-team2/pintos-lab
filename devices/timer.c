#include "devices/timer.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include "threads/interrupt.h"
#include "threads/io.h"
#include "threads/synch.h"
#include "threads/thread.h"

/* See [8254] for hardware details of the 8254 timer chip. */

/*
============================================
       시스템 타이머 (초당 100틱) 
      이 프로젝트에서 수정 대상이다.
============================================
*/

#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19
#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif

/* Number of timer ticks since OS booted. */
static int64_t ticks;
/*
=========================================
        [next_tick_to_awake]

       최소 틱을 가진 스레드 저장

 현재 sleep 중인 스레드들 중 가장 빨리
   깨어나야 할 시간을 저장하는 변수

=> 즉 이 시간이 되기 전까지는 굳이 어떤 
   스레드가 있는지 리스트를 순회하러
   안가도 됨 !

[왜 필요할까?]
 매 tick마다 sleep_list를 전부 순회하면서
 깨어날 스레드를 찾는 것은 비효율적이다.

 => 이 변수를 사용해서 아직 깰 시간이 안됨?
    굳이 리스트를 순회하지 않도록 최적화
=========================================
*/
static int64_t next_tick_to_awake; 


/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;

static intr_handler_func timer_interrupt;
static bool too_many_loops (unsigned loops);
static void busy_wait (int64_t loops);
static void real_time_sleep (int64_t num, int32_t denom);

/*
===============================================
          내가 새로 생성해준 함수들
1.void update_next_tick_to_awake(int64_t tick)

===============================================

*/

void update_next_tick_to_awake(int64_t ticks){
/*
=============================================
   long long 형 int 형 ticks를 매개변수로 !

   next_tick_to_awake가 
   깨워야 할 스레드 중
   가장 작은 tick을 갖도록 업데이트 해야함 

   삼항연산자 사용해서 값 업데이트 시켜주기 !
=============================================
*/  

next_tick_to_awake = (next_tick_to_awake > ticks) ? ticks : next_tick_to_awake;

}

/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */
void
timer_init (void) {
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest. */
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;

	outb (0x43, 0x34);    /* CW: counter 0, LSB then MSB, mode 2, binary. */
	outb (0x40, count & 0xff);
	outb (0x40, count >> 8);

	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
}

/* Calibrates loops_per_tick, used to implement brief delays. */
void
timer_calibrate (void) {
	unsigned high_bit, test_bit;

	ASSERT (intr_get_level () == INTR_ON);
	printf ("Calibrating timer...  ");

	/* Approximate loops_per_tick as the largest power-of-two
	   still less than one timer tick. */
	loops_per_tick = 1u << 10;
	while (!too_many_loops (loops_per_tick << 1)) {
		loops_per_tick <<= 1;
		ASSERT (loops_per_tick != 0);
	}

	/* Refine the next 8 bits of loops_per_tick. */
	high_bit = loops_per_tick;
	for (test_bit = high_bit >> 1; test_bit != high_bit >> 10; test_bit >>= 1)
		if (!too_many_loops (high_bit | test_bit))
			loops_per_tick |= test_bit;

	printf ("%'"PRIu64" loops/s.\n", (uint64_t) loops_per_tick * TIMER_FREQ);
}

/* Returns the number of timer ticks since the OS booted. */
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
}

/* Suspends execution for approximately TICKS timer ticks. */

/*
==============================================
 우리가 alarm_clock() 프로젝트에서 수정구현
 초본 함수는 busy waiting(바쁜 대기) 방식을 사용
 
 현재 시간을 계속 확인하면서 thread_yield()를 호출
 루프를 돌며 시간이 충분히 지날 때까지 기다린다.

 이 함수를 바쁜 대기를 사용하지 않는 방식으로 재구현


*/
void
timer_sleep (int64_t ticks) {
/*
========================================================
   호출된 스레드의 실행을 ticks이 지날 때 까지 중단(잠자기)
   이 함수의 인자는 밀리초나 다른 단위가 아닌
   timer 틱 단위이다.

   초당 틱 수는 devices/timer.h에 정의 된 매크로 값
   기본 값수는 초당 100

   이 값을 변경하면 테스트 실패 가능성 커짐 변경 ㄴ ㄴ
========================================================
*/

	int64_t start = timer_ticks ();

	ASSERT (intr_get_level () == INTR_ON);
    // 이 위의 줄까지는 기본 설정 코드


	/*
	while (timer_elapsed (start) < ticks)
		thread_yield ();
	기존의 busy_waiting을 유발하는 코드 삭제.
    */

   thread_sleep(start + ticks); 
   /*
   ==============================================
              원래 시작시간 + 틱 시간
       언제 깨어나야 하는지를 나타내는 시간이다.
   ==============================================
   */
/*
========================
thread_yield () 
현재 실행 중인 스레드 
cpu에서 내려오게 하고 
ready 상태로 되돌리기

즉,스스로 양보하기 ! 


[언제 쓰일까]

while() 같은 loop안에서
 sleep을 흉내
 sleep이 아니라 ready
 
따라서 ready 리스트로 들어감
조건이 충족되면 다시 실행
========================
*/
}

/* Suspends execution for approximately MS milliseconds. */
void
timer_msleep (int64_t ms) {
	real_time_sleep (ms, 1000);
}

/* Suspends execution for approximately US microseconds. */
void
timer_usleep (int64_t us) {
	real_time_sleep (us, 1000 * 1000);
}

/* Suspends execution for approximately NS nanoseconds. */
void
timer_nsleep (int64_t ns) {
	real_time_sleep (ns, 1000 * 1000 * 1000);
}

/* Prints timer statistics. */
void
timer_print_stats (void) {
	printf ("Timer: %"PRId64" ticks\n", timer_ticks ());
}

/* Timer interrupt handler. */
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	ticks++; //틱 증가해주면서 조건에 해당되는 틱 검사 ? 
    /*
	=============================================================
	             여기 안에서 free 해주면 안됨 !
	        동시에 깨워줘야 할 스레드가 있을 수도 있음 
	    깨울 스레드가 더 있는데 리스트 구조가 깨진다 => 시스템 터짐
    =============================================================
	*/

	if(get_next_tick_to_awake() <= ticks){
		thread_awake(ticks);
	}
	thread_tick ();
	//시간이 됐는지 check 해주기
	//timer_tick 으로 현재 틱 구해주기 가능 
}

/* Returns true if LOOPS iterations waits for more than one timer
   tick, otherwise false. */
static bool
too_many_loops (unsigned loops) {
	/* Wait for a timer tick. */
	int64_t start = ticks;
	while (ticks == start)
		barrier ();

	/* Run LOOPS loops. */
	start = ticks;
	busy_wait (loops);

	/* If the tick count changed, we iterated too long. */
	barrier ();
	return start != ticks;
}

/* Iterates through a simple loop LOOPS times, for implementing
   brief delays.

   Marked NO_INLINE because code alignment can significantly
   affect timings, so that if this function was inlined
   differently in different places the results would be difficult
   to predict. */
static void NO_INLINE
busy_wait (int64_t loops) {
	while (loops-- > 0)
		barrier ();
// 바쁜 대기 
}

/* Sleep for approximately NUM/DENOM seconds. */
static void
real_time_sleep (int64_t num, int32_t denom) {
	/* Convert NUM/DENOM seconds into timer ticks, rounding down.

	   (NUM / DENOM) s
	   ---------------------- = NUM * TIMER_FREQ / DENOM ticks.
	   1 s / TIMER_FREQ ticks
	   */
	int64_t ticks = num * TIMER_FREQ / denom;

	ASSERT (intr_get_level () == INTR_ON);
	if (ticks > 0) {
		/* We're waiting for at least one full timer tick.  Use
		   timer_sleep() because it will yield the CPU to other
		   processes. */
		timer_sleep (ticks);
	} else {
		/* Otherwise, use a busy-wait loop for more accurate
		   sub-tick timing.  We scale the numerator and denominator
		   down by 1000 to avoid the possibility of overflow. */
		ASSERT (denom % 1000 == 0);
		busy_wait (loops_per_tick * num / 1000 * TIMER_FREQ / (denom / 1000));
	}
}
