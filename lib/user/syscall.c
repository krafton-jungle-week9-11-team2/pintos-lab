#include <syscall.h>
#include <stdint.h>
#include "../syscall-nr.h"

/*
=======================================================
                   유저 영역 함수

-유저 프로그램이 커널에 시스템 콜을 호출할 수 있도록


-시스템 콜 번호와 인자를 정해진 레지스터에 넣고 
 syscall명령을 실행하는 인터페이스를 제공

=======================================================
*/



/*
=====================================================
-항상 인라인되도록 지정된 함수 
 => 인라인 실행 이란 ?
    -함수를 호출하는 대신 , 함수 본문을 그대로 복사해서 
	 실행하는 방식



-유저 모드에서 커널로 시스템 콜을 호출하기 위한 인터페이스
-최대 6개의 인자를 받아 syscall 명령어를 통해 
 커널에 요청을 보냄
*/
__attribute__((always_inline)) //인라인(복사)하라고 강제 
static __inline int64_t syscall (uint64_t num_, uint64_t a1_, uint64_t a2_,
		uint64_t a3_, uint64_t a4_, uint64_t a5_, uint64_t a6_) {
	int64_t ret; //커널에서 반환될 결과 값을 저장할 변수
	
    /*
	===========================================================
      -시스템 콜 번호 및 인자들을 해당하는 레지스터에 바인딩
	  -각 레지스터는 x86-64 시스템 콜 호출 규약에 따라 사용됨

	  rax,rdi,rsi,rdx,r10,r8,r9

	  syscall 번호,첫번째 인자,두번째 인자,세번째 인자
	  네번째 인자,다섯번째 인자,여섯 번째 인자

	===========================================================
	*/
	register uint64_t *num asm ("rax") = (uint64_t *) num_;
	register uint64_t *a1 asm ("rdi") = (uint64_t *) a1_;
	register uint64_t *a2 asm ("rsi") = (uint64_t *) a2_;
	register uint64_t *a3 asm ("rdx") = (uint64_t *) a3_;
	register uint64_t *a4 asm ("r10") = (uint64_t *) a4_;
	register uint64_t *a5 asm ("r8") = (uint64_t *) a5_;
	register uint64_t *a6 asm ("r9") = (uint64_t *) a6_;


    /*
	==============================================================
	인라인 어셈블리 : 레지스터에 값을 넣고 syscall명령 실행
	
	volatile은 컴파일러가 이 코드를 최적화로 제거하지 않도록 함.
	==============================================================
	*/
	__asm __volatile(
			"mov %1, %%rax\n"  //시스템 콜 번호
			"mov %2, %%rdi\n"  //첫 번째 인자
			"mov %3, %%rsi\n"  //두 번째 인자
			"mov %4, %%rdx\n"  //세 번째 인자
			"mov %5, %%r10\n"  //네 번째 인자(함수 호출)
			"mov %6, %%r8\n"   //다섯 번째 인자
			"mov %7, %%r9\n"   //여섯 번째 인자
			"syscall\n"   //시스템 콜 호출(커널 모드로 진입)
			: "=a" (ret) //출력 : 커널에서 반환된 값은 rax->ret으로 저장
			: "g" (num), "g" (a1), "g" (a2), "g" (a3), "g" (a4), "g" (a5), "g" (a6)
			: "cc", "memory"); //부수효과 : condition code와 메모리가 바뀜을 알림
	return ret;  //시스템 콜 결과값 반환
}

/* Invokes syscall NUMBER, passing no arguments, and returns the
   return value as an `int'. 
   
   -인자가 없는 시스템 콜을 호출한다.
   -NUMBER는 시스템 콜 번호이며 , 결과는 int형으로 반환된다.
   -나머지 인자 6개는 모두 0으로 채워진다.
   */
#define syscall0(NUMBER) ( \
		syscall(((uint64_t) NUMBER), 0, 0, 0, 0, 0, 0))

/* 
   Invokes syscall NUMBER, passing argument ARG0, and returns the
   return value as an `int'. 
   
   -모든 시스템 콜은 최대 6개의 인자를 가질 수 있고, 그 이상은 사용할 수 없음

   -이 매크로들은 호출하는 시스템 콜의 인자 수에 따라 적절한 수의 값을 전달,
    나머지는 0으로 패딩해줌

   -이는 내부적으로 syscall() 함수로 연결되며,
    rax,rdi,rsi,rdx,r10,r8,r9 순서로 레지스터에 들어가게 된다.
*/
#define syscall1(NUMBER, ARG0) ( \
		syscall(((uint64_t) NUMBER), \
			((uint64_t) ARG0), 0, 0, 0, 0, 0))
/* Invokes syscall NUMBER, passing arguments ARG0 and ARG1, and
   returns the return value as an `int'. */
#define syscall2(NUMBER, ARG0, ARG1) ( \
		syscall(((uint64_t) NUMBER), \
			((uint64_t) ARG0), \
			((uint64_t) ARG1), \
			0, 0, 0, 0))

#define syscall3(NUMBER, ARG0, ARG1, ARG2) ( \
		syscall(((uint64_t) NUMBER), \
			((uint64_t) ARG0), \
			((uint64_t) ARG1), \
			((uint64_t) ARG2), 0, 0, 0))

#define syscall4(NUMBER, ARG0, ARG1, ARG2, ARG3) ( \
		syscall(((uint64_t ) NUMBER), \
			((uint64_t) ARG0), \
			((uint64_t) ARG1), \
			((uint64_t) ARG2), \
			((uint64_t) ARG3), 0, 0))

#define syscall5(NUMBER, ARG0, ARG1, ARG2, ARG3, ARG4) ( \
		syscall(((uint64_t) NUMBER), \
			((uint64_t) ARG0), \
			((uint64_t) ARG1), \
			((uint64_t) ARG2), \
			((uint64_t) ARG3), \
			((uint64_t) ARG4), \
			0))


/*
=================================================================

 halt(void)

-시스템 종료 요청을 커널에 전달한다.
-SYS_HALT은 halt syscall 번호(상수)이며
 syscall0은 인자가 없는 syscall을 호출한다.
-호출 후 커널이 종료되므로, 이후 코드는 실행되지 않아야 하며
 NOT_REACHED 매크로로 표시한다.

=================================================================
*/
void
halt (void) {
	syscall0 (SYS_HALT);
	NOT_REACHED (); //커널이 종료되므로 여기는 실행되면 안됨(매크로)
}

/*
==================================================================

exit(void)

-현재 프로세스를 종료하고, 종료 코드를 커널에 전달한다.
-SYS_EXIT 시스템 콜 번호와 함께 "status 값을" 인자로 syscall1을 호출한다.
-이후의 코드는 실행되지 않음(매크로)



===================================================================
*/

void
exit (int status) {
	syscall1 (SYS_EXIT, status);
	NOT_REACHED ();
}

/*
==================================================================

fork()

-현재 프로세스를 복제하여 자식 프로세스를 생성한다.
-SYS_FORK 시스템 콜 번호와 "thread_name"을 전달
-반환값으로 자식의 pid(process ID)를 ,자식은 0을 받게 된다.(커널 처리)

==================================================================
*/
pid_t
fork (const char *thread_name){
	return (pid_t) syscall1 (SYS_FORK, thread_name);
}

/*
==================================================================
exec

-현재 프로세스를 주어진 "실행 파일로 대체"
-SYS_EXEC 시스템 콜 번호와 실행할 파일명을 전달
-성공 시 반환되지 않고, 실패하면 -1을 반환한다.

==================================================================
*/

int
exec (const char *file) {
	return (pid_t) syscall1 (SYS_EXEC, file);
}

/*
==================================================================

wait

-주어진 pid의 자식 프로세스가 종료될 때까지 대기한다.
-SYS_WAIT 시스템 콜 번호와 "대기할 자식의 pid를 전달"
-자식이 종료되면 exit status를 반환받는다.

==================================================================
*/

int
wait (pid_t pid) {
	return syscall1 (SYS_WAIT, pid);
}

/*
==================================================================
create

-파일 시스템에 새 파일을 생성하는 시스템 콜을 호출한다.
-file : 생성할 파일의 이름(문자열 포인터)
-initial_size : 파일의 초기 크기 (바이트 단위)
-SYS_CRATE: create_syscall 번호
-syscall2를 통해 인자를 전달 , 생성 성공 여부로 bool 형으로 반환

==================================================================
*/

bool //bool형 반환한다.
create (const char *file, unsigned initial_size) {
	return syscall2 (SYS_CREATE, file, initial_size);
}

/*
==================================================================
remove

-파일 : 시스템에서 주어진 파일을 삭제하는 시스템 콜을 호출
-file : 삭제할 파일 이름

-SYS_REMOVE : remove syscall 번호
-syscall로 파일 이름을 넘기고, 삭제 성공 여부를 bool로 반환


==================================================================
*/

bool
remove (const char *file) {
	return syscall1 (SYS_REMOVE, file);
}

/*
==================================================================
open :

-주어진 이름의 파일을 연다. 성공하면 파일 디스크립터를 반환
-file : 열고자 하는 파일 이름
-SYS_OPEN : open syscall 번호

-syscall로 파일 이름을 넘기고, 결과는 열린 "파일의 FD(정수형)" 이다. 
-실패 시 -1을 반환할 수 있다.


==================================================================
*/

int
open (const char *file) {
	return syscall1 (SYS_OPEN, file);
}

/*
==================================================================
filesize 

-열린 파일의 크기를 반환한다.
-fd : 파일 디스크립터 (open을 통해 얻은 값)

-syscall로 fd를 넘기고, 해당 파일의 크기(바이트)를 반환 한다.

==================================================================
*/

int
filesize (int fd) {
	return syscall1 (SYS_FILESIZE, fd);
}

/*
==================================================================
read 

-열린 파일 디스크립터부터 데이터를 읽는다.

[인자 분석]
-fd : 읽을 대상 파일 디스크립터
-buffer : 데이터를 저장할 버퍼 (사용자 메모리 주소)
-size : 읽을 바이트 수
-SYS_READ : read syscall 번호

-syscall3을 사용하여 fd,buffer,size 세 개의 인자를 전달한다.

[반환 값]
-실제로 읽은 바이트 수이고, 실패시 -1을 반환할 수 있다.
==================================================================
*/

int
read (int fd, void *buffer, unsigned size) {
	return syscall3 (SYS_READ, fd, buffer, size);
}

/*
==================================================================
wirte

[인자 분석]
-size : 쓸 바이트 수
-반환값은 실제로 쓴 바이트 수 , 실패시 -1 을 반환할 수 있다. 


==================================================================
*/

int
write (int fd, const void *buffer, unsigned size) {
	return syscall3 (SYS_WRITE, fd, buffer, size);
	/*
	===================================================================
	1. 내부 syscall() 호출하기
	2. syscall() 함수 내부에서는 
	   -rax,rdi,rsi,rdx 레지스터 설정
	3.syscall 어셈블리 명령어 실행 -> 커널 진입

	4.커널 코드(kernel space) =>userprog/syscall.c
	5.커널 내부의 sys_write() 함수가 실행됨 (내가 직접 구현해야 하는 부분)
    ===================================================================
	*/
}

/*
==================================================================
seek 
-파일 디스크립터의 읽기/쓰기 포인터를 지정한 위치로 이동시킨다.
-position : 이동할 바이트 위치(파일 시작 기준 오프셋)

-SYS_SEEK : seek syscall 번호
-syscall2를 사용하여 fd와 위치 인자(position)를 전달한다.

-반환값은 없으며,위치 이동만 수행된다. 
==================================================================
*/

void // 반환값 없음
seek (int fd, unsigned position) {
	syscall2 (SYS_SEEK, fd, position);
}

/*
==================================================================
tell
-현재 파일 디스크립터의 읽기/쓰기 포인터 위치를 반환

-syscall1을 통해 fd만 전달한다.

-반환값은 현재 오프셋 위치 (바이트 단위)이다.

==================================================================
*/

unsigned  // 부호가 없는 수를 반환 (오프셋은 음수 불가능) 
tell (int fd) {
	return syscall1 (SYS_TELL, fd);
}

/*
==================================================================
close

-열린 파일 디스크립터를 닫는다.
-syscall1로 fd만 전달하면 되고, 반환값은 없다.

-커널 내부에서 자원 해제와 파일 핸들 정리를 수행한다.
==================================================================
*/

void
close (int fd) {
	syscall1 (SYS_CLOSE, fd);
}


/*
=====================================================================
   이 아래의 함수 부터는 project2에서는 테스트되지도 않고 호출도 안됨
     
	 dup2만 project2 추가구현에서만 필요 나머지는 3이후 부터 필요
   
=====================================================================
*/

int
dup2 (int oldfd, int newfd){
	return syscall2 (SYS_DUP2, oldfd, newfd);
}

void *
mmap (void *addr, size_t length, int writable, int fd, off_t offset) {
	return (void *) syscall5 (SYS_MMAP, addr, length, writable, fd, offset);
}

void
munmap (void *addr) {
	syscall1 (SYS_MUNMAP, addr);
}

bool
chdir (const char *dir) {
	return syscall1 (SYS_CHDIR, dir);
}

bool
mkdir (const char *dir) {
	return syscall1 (SYS_MKDIR, dir);
}

bool
readdir (int fd, char name[READDIR_MAX_LEN + 1]) {
	return syscall2 (SYS_READDIR, fd, name);
}

bool
isdir (int fd) {
	return syscall1 (SYS_ISDIR, fd);
}

int
inumber (int fd) {
	return syscall1 (SYS_INUMBER, fd);
}

int
symlink (const char* target, const char* linkpath) {
	return syscall2 (SYS_SYMLINK, target, linkpath);
}

int
mount (const char *path, int chan_no, int dev_no) {
	return syscall3 (SYS_MOUNT, path, chan_no, dev_no);
}

int
umount (const char *path) {
	return syscall1 (SYS_UMOUNT, path);
}
