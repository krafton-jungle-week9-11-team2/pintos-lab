#include "devices/timer.h" // 	이 파일에 대응되는 헤더! 함수 선언, 매크로, 전역 변수 등
#include <debug.h> // 	ASSERT() 같은 디버깅용 함수 사용 가능
#include <inttypes.h> // int64_t, PRIu64 같은 고정된 크기의 정수 타입 사용
#include <round.h> // 반올림 계산 도와주는 매크로들
#include <stdio.h> // printf() 쓸 수 있게 해줌
#include "threads/interrupt.h" // 인터럽트 설정/비활성화 등을 위한 도구
#include "threads/io.h" // 포트 입출력 (outb() 같은 저수준 함수)
#include "threads/synch.h" // 	세마포어, 락 등 동기화 도구들 (알람 시계 과제에선 직접 안 씀)
#include "threads/thread.h" // 	스레드 관련 함수 및 구조체 (thread_current(), thread_block() 등)

/* See [8254] for hardware details of the 8254 timer chip. */
// #if, #error는 컴파일 조건 검사
#if TIMER_FREQ < 19
#error 8254 timer requires TIMER_FREQ >= 19

#endif
#if TIMER_FREQ > 1000
#error TIMER_FREQ <= 1000 recommended
#endif
// “TIMER_FREQ 값이 19보다 작거나, 1000보다 크면 컴파일하지 마!”
// 하드웨어 제약 조건이니까 잘못 쓰면 시스템이 망가질 수도 있어서 미리 막아주는 것임.

//int64_t  정확히 64비트를 보장해주는 정수형
/* Number of timer ticks since OS booted. */
static int64_t ticks;
//  Pintos가 부팅된 이후로 지난 틱 수를 세는 전역 변수임. 
// 1틱 = 1/100초 (기본 TIMER_FREQ가 100이니까)
// 전역이라서: timer_ticks() 함수 같은 데서 언제든 참조 가능!
// 그냥 정해 놓은 것임 -> 실제 컴퓨터는 더 빠름. 즉, 절대값이 아니라 기준값 
// 바꾸면 안돼? 테스트케이스가 다 그 값을 기준으로 설정되어 있어서 문제가 생길 수도 있음. 


/* Number of loops per timer tick.
   Initialized by timer_calibrate(). */
static unsigned loops_per_tick;
// 이건 정확한 시간 측정을 위한 내부 루프 횟수야.
// busy_wait() 같은 함수에서 “이만큼 돌리면 1틱 걸린다”는 기준으로 쓰여.
// 당장 알람 시계 과제에서는 필요하지 않은 변수 


static intr_handler_func timer_interrupt;
// 타이머 인터럽트가 발생했을 때 호출될 함수 포인터 선언 
// 포인터 선언한 이유 intr_register_ext() 같은 함수에서 인터럽트 핸들러를 함수 포인터로 등록함.
// 따라서 intr_register_ext(0x20, timer_interrupt, "8254 Timer");
// 이런식을 쓸 수 있어야 하니까 타입을 맞춰주는 선언이 필요함 .

static bool too_many_loops (unsigned loops);
//  loops_per_tick 값을 자동으로 조정할 때 기준이 되는 함수 선언
// 나중에 timer_calibrate() 함수에서 이걸 씀 -> 어떤 값을 루프로 돌려보고, 너무 오래걸리면 많다고 판단함. 
// 필요한 이유: 정확한 시간 측정이 필요할때 busy_wait를 쓸 수 있도록 보정하기 위해.


static void busy_wait (int64_t loops);
// 말 그대로 무의미한 루프를 일정 횟수만큼 돌리는 함수야
// thread_block() 같은 진짜 잠자기 쓰기엔 너무 짧을 때, 그냥 CPU를 계속 쓰면서 루프만 돌려서 기다리는 방법이야.
static void real_time_sleep (int64_t num, int32_t denom);
// 실제 시간을 기준으로 잠자기 위한 함수 
// timer_sleep()을 호출하거나 busy_wait()을 써서 아주 짧게 기다림.



/* Sets up the 8254 Programmable Interval Timer (PIT) to
   interrupt PIT_FREQ times per second, and registers the
   corresponding interrupt. */
// 이 함수는 Pintos가 부팅될 때 실행돼서, 타이머 칩(8254)을 설정하고,
// '틱!' 소리(=인터럽트) 를 1초에 TIMER_FREQ번 울리게 만들어주는 함수
void
timer_init (void) {
	/* 8254 input frequency divided by TIMER_FREQ, rounded to
	   nearest. */
		// 타이머 칩에게 “11932마다 한 번씩 인터럽트 쏴줘”라고 설정하는 것
		// 클럭을 11932 쏘는것이 아님. 타이머 칩의 카운트가 11932만큼 기다렸다가 ->  11932가 어떻게 보면 1/100초 그거임
	uint16_t count = (1193180 + TIMER_FREQ / 2) / TIMER_FREQ;
 // count = 입력 주파수 ÷ 출력 주파수
 //TIMER_FREQ = 100
// count = (1193180 + 50) / 100 = 11932
	outb (0x43, 0x34);  //I/O 포트에 바이트 1개를 직접 보내는 함수  /* CW: counter 0, LSB then MSB, mode 2, binary. */
	//진짜 하드웨어 제어에 사용돼 (CPU가 직접 칩에 말하는 거!)

//카운트 값 전송
	outb (0x40, count & 0xff); // LSB (하위 8비트만 추출) 
	outb (0x40, count >> 8); // MSB (상위 8비트)
	//타이머 칩에게 우리가 계산한 주기를 2바이트로 쪼개서 전송하는 거임 


	intr_register_ext (0x20, timer_interrupt, "8254 Timer");
	//인자: 타이머 인터럽트 번호, 타이머 틱이 발생할 때 호출될 함수, 디버깅용 이름
	//즉, 타이머 칩이 11932번 세면 인터럽트(0x20) 발생함
  //  CPU는 인터럽트 발생 시 timer_interrupt() 실행

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
//운영체제가 부팅된 이후로 지금까지 흐른 총 틱 수(ticks) 를 반환해주는 함수
int64_t
timer_ticks (void) {
	enum intr_level old_level = intr_disable ();
	int64_t t = ticks;
	intr_set_level (old_level);
	barrier ();
	return t;
	// 총 틱수 반환 
}

/* Returns the number of timer ticks elapsed since THEN, which
   should be a value once returned by timer_ticks(). */
int64_t
timer_elapsed (int64_t then) {
	return timer_ticks () - then;
	// 현재 시점의 틱 수(timer_ticks)는 계속 변함
// 'then'은 잠자기 시작했을 때의 틱 수 (start)
// → 이 둘의 차이를 구하면, 잠든 이후 지난 시간(틱 수)이 됨

}

/* Suspends execution for approximately TICKS timer ticks. */
// 목표: 틱만큼 잠자게 해주는 함수 
void
timer_sleep (int64_t ticks) {
	//ticks -> 자러 들어간 틱수
	int64_t start = timer_ticks ();
	// timer_ticks 이거 뭐냐면 지금까지 운영체제가 부팅된 이후 지난 틱수를 반환함.
	// 그걸 start 변수에 저장 .
	ASSERT (intr_get_level () == INTR_ON);
	//디버깅용 안전장치
	//  인터럽트가 켜져 있어야 제대로 작동함. -> 그래서 인터럽트가 켜져있는지를 확인하는 함수.
	while (timer_elapsed (start) < ticks)
// "start 이후 몇 틱이 흘렀을까?"를 알려주는 함수
// → 현재 시각 - start 시각
// 그게 내가 잠잘 시간보다 같기 전까지 계속 돔 
		thread_yield ();
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
// 인터럽트 핸들러 !! 
static void
timer_interrupt (struct intr_frame *args UNUSED) {
	// struct intr_frame 는 인터럽트가 발생했을 때, cpu 상태(레지스터, 플래그)를 담은 구조체
	// UNUSED -> 인자로 받긴 하지만 안쓴다는 얘기
	ticks++; // 1/100초마다 호출됨 (10ms)
	thread_tick();   // 현재 스레드의 틱도 증가 -> thread_ticks 이게 증가 
	// thread_ticks 는 문맥전환되면 자동으로 0으로 초기화됨  (thread_launch에서)
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
