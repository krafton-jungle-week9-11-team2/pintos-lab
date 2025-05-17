#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/*
========================================================================
  -유저 프로세스가 일부 커널 기능에 접근하려고 할 때 시스템 콜 호출

  즉 이 파일에 구현되어 있는 코드들이 !!!!

   ***시스템 콜 핸들러의 기본 구조***

  -현재 상태에서는 유저가 시스템 콜을 했을 때 
   =>단지 메새지를 출력하고 유저 프로세스를 종료시키게 되어있다 !

  -이번프로젝트의 epic2(Basic file Operation)에서 
   시스템 콜이 필요로 하는 다른 일을 수행하는 코드를 수행하게 된다.

   syscall_handler 안에서 호출되어 지는 open(),read(),write() 등의 함수
   syscall.c 안에서 구현하기
========================================================================
*/

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
			//유저 코드 세그먼트와 커널 코드 세그먼트를 시스템 콜용으로 설정
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);
	//이 값은 세크먼트 전환용으로 syscall이 참조할 값

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	// syscall시 특정 EFLAGS 비트를 자동으로 클리어하여 , 인터럽트 등 방해가 없게 !
}

/* 
======================================================================
      The main system call interface - syscall_handler
   -epic 2에서 우리가 구현/수정 해줘야 할 함수
   -실제로 유저 프로그램이 시스템 콜을 실행했을 때 호출되는 함수

   - " system call ! " 이라는 구문 출력 후 프로세스 바로 종료 
   - 이 안에서 시스템 콜 번호 파싱 
   - f -> R.rax 에 syscall 번호 , f -> R.rdi 등에는 인자 전달

======================================================================
*/
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	printf ("system call!\n");
	thread_exit ();
}
