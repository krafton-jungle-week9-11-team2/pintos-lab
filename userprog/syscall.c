#include "filesys/filesys.h" // 잡았다 요놈!
#include "userprog/process.h" // 잡았다 요놈! 
#include "filesys/file.h" //close때 안해주면 오류남 ! 

#include "threads/palloc.h"
#include "userprog/syscall.h"


#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/init.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"


void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void check_address(const uint64_t *addr);
static struct file *process_get_file(int fd);
//int add_file_to_fdt(struct file *file);


/* Forward declarations for system call handlers */
void halt(void);
void exit(int status);
tid_t fork(const char *name, struct intr_frame *f);
int exec(const char *file_name);
int open(const char *file);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned length);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);



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

#define PAL_ZERO 0

/*
==================================
 주소값이 유저 영역 내인지를 검증

     param addr : 주소값

==================================
*/

void check_address(const uint64_t *addr){

     struct thread *cur = thread_current();

     if(addr == NULL || !(is_user_vaddr(addr)) || pml4_get_page(cur->pml4,addr) == NULL)
          exit(-1);
}



void halt(void){
     //printf("[kernel] HALT syscall received!\n");
     power_off();
     //init.h & init.c 에 구현되어 있음
     //pintos를 poweroff 하는 함수 
}

//프로세스 종료 시스템 콜
void exit(int status){

     struct thread *cur =thread_current();
     // 현재 스레드 
     cur->exit_status=status; //정상적 종료시 0

     printf("%s: exit(%d)\n",thread_name(),status);
     //therad_name 함수는 thread구조체에 접근해서 name 값 가져온다.
     //트러블 슈팅 (와 이거 한 줄 때문에 args테스트 안됐던 거였음) 
     thread_exit();

     /*ver1. wait중인 부모 프로세스를 깨울 방법이 없어서 종료상태만
       저장 후 끝낸다 */
}


/*
====================================
write

-buffer로 부터 open file fd로 
 size 바이트를 적어준다.

-실제로 적힌 바이트의 수를 반환해주고(int)
 일부 바이트가 적히지 못했다면 
 size보다 더 작은 바이트 수가 반환 가능

-더 이상 바이트를 적을 수 없다면 0 반환

-putbuf()를 통해 한번에 호출하여 모든 내용을
 버퍼에 담는다.
====================================
*/

int write(int fd,const void *buffer,unsigned size)
{
    /*
    ==============================================================
    ver1 . 임시로 표준출력만 구현 

     *buffer : 유저 공간에 있는 메모리 주소 

     -유저 프로그램이 출력하고자 하는 
     데이터를 저장한 메모리 버퍼의 시작 주소 지점 ! 

     -유저 프로세스가 관리하는 가상 메모리 상의 주소이며, 
      이 주소가 커널 내부에서 바로 접근되면 안됨 !
       =>검증 절차가 반드시 필요함 !

     fd : 파일 디스크 립터 => 출력하려는 대상(콘솔,파일 등)을 가리킴
     buffer : 출력할 데이터가 담긴 유저 메모리 영역의 포인터
     size : 출력할 데이터의 크기( 바이트 단위 )
    ==============================================================
    */
     check_address(buffer); // 주소 유효성 검사
     if(fd == 1)
     {

          putbuf(buffer,size); //   lib/kernel/console.c 에 있음
          return size;
     }

     return -1;

}

/*
=====================================================
[create]

-file이라는 이름의 새 파일을 만들고 초기 크기 바이트 설정
-성공하면 true, 실패라면 flase를 반환

[주의 할 점]
-새 파일을 만드는 것은 파일을 여는건 아님
-파일을 여는것은 'open' 이라는 시스템 사용해서 열어야됨
=====================================================
*/



bool create(const char *file, unsigned initial_size) {
     check_address(file); //유저가 넘겨준 file주소 유효성 검사

     bool success; //반환값 위해 변수 생성

     if(file == NULL || initial_size < 0 ){
          return 0 ; // 0 이 false임
     }

     success =filesys_create(file,initial_size);
     //filesys_create 함수가 다해주는 듯 
     return success;
}


/*
===========================================
        파일을 삭제하는 시스템 콜
filesys_remove() 사용  (filesys/filesys.c)
===========================================
*/
bool remove(const char *file) {
     check_address(file);
     return filesys_remove(file);
}


/*
=====================================
현재 프로세스(스레드)의 fd_table(자료구조)
 => 새로운 파일을 추가하는 함수

-열려 있는 파일 목록에 새 파일을 등록
-그 파일의 파일 디스크립터 번호를 반환


=====================================
*/

int process_add_file(struct file *file){
//현재 프로세스의 fdt에 파일(fd)을 추가해주기

     struct thread *curr =thread_current();
     struct file **fdt = curr ->fd_table;
     //현재 스레드가 갖고 있는 fd_table (파일 포인터 배열)

     //curr->next_fd :다음에 사용할 수 있는(비어있는) fd 번호 후보
	while(curr->next_fd < FDCOUNT_LIMIT && fdt[curr->next_fd] )
     {
          curr->next_fd++;
          //다음 fd 번호 후보 증가시키면서 빈 번호(자리) 탐색 
     }

     if(curr->next_fd >= FDCOUNT_LIMIT)
        return -1; //자리 없음 

     
     fdt[curr->next_fd] = file; //빈 슬록에 새 파일 포인터 할당
     return curr->next_fd; //새로 할당된 fd 번호 리턴 
    
}


/*
======================================
[open]

 반환값이 fd(파일 디스크립터)
  => 파일 식별하는 숫자

  파일 디스크립터는 process_add_file()을 
  통해 현재 프로세스의 fd_table에 저장됨
  
======================================
*/

int open(const char *file) {

    check_address(file);

    struct file *open_file = filesys_open(file);
    //open 해준 파일 지정해주는 변수

    if(open_file == NULL){
       
       return -1;
    }

    int fd=process_add_file(open_file);
    //파일을 열면 fdt에 추가해줘야됨 

   
    
    //fd table 가득 찼다면 파일 닫아주기 ?
    if(fd == -1){
       file_close(open_file);
       
    }

    return fd;

}


/*
====================================
[close]      
     파일 fd를 닫는 시스템 콜

-프로세스가 종료되거나 중지될 때 

-모든 열린 파일 디스크립터는 자동으로
 이 함수를 호출하여 닫아야한다. 

====================================
*/


void close(int fd) {

     struct file *file_fd =process_get_file(fd);
     if(file_fd == NULL)
     {
          return -1;
     }

     file_close(file_fd); //파일 닫기(내부에서 메모리 해제 포함)
     remove_fd(fd);//우리가 구현해줘야됨 

     return 0;

}

void remove_fd(int fd){
     struct thread *cur =thread_current();

     if(fd<2 || fd >= FDCOUNT_LIMIT)
       return; //유효하지 않은 fd면 무시
     
     cur->fd_table[fd]=NULL;
     //0인지 NULL인지 . . .
}


//파일 크기 구하기
int filesize(int fd) {
     int file_len;
     struct file *temp;

     temp=process_get_file(fd);

     if(temp == NULL)
        return -1;

     file_len =file_length(temp); 
     /*
     ========================================
               file.c에 구현되어있음
     타고타고 들어가서 결국 inode_disk에 있는
     'off_t(int32_t) length' 크기 가져오기 ! 
     ========================================
     */

     return file_len;
    
}

/*
==========================================

   파일 디스크립터 fd가 유효한지 검사한 뒤
   현재 실행 중인 스레드의 fd_table에서

 해당 fd에 연결된 struct file 포인터를 반환

 fd_table은 현재 프로세스의 열려 있는 파일들의 배열

==========================================
*/

static struct file *process_get_file(int fd){
     struct thread *cur =thread_current();

     if(fd < 0 || fd>=FDCOUNT_LIMIT){
          return NULL;
     }
     return cur->fd_table[fd];
}

/*
======================================================
-fd로부터 열린 파일의 크기를 읽고 버퍼에 담는다.
-실제로 읽은 파일의 크기를 반환하며 
-실패시에는 -1 반환

-fd가 0이면 input_getc() 를 이용해서 
 => 키보드 입력을 읽어온다.

-주어진 파일 디스크립터 번호와 일치하는 파일 디스크립터를
 찾아 그 파일을 읽어들이기!
======================================================
*/
int read(int fd, void *buffer, unsigned length) {
     check_address(buffer); //주소 유효성 검사

     if(fd == 0){
          return input_getc();
          //fd =0 이면 키보드에서 받기
     }

     //fd !=0 일 때
     struct file *f =process_get_file(fd);
     if (f == NULL){
          return 0;
     }

     return file_read(f,buffer,length);
    
}



/*
==========================================
아래 임시로 적어놓은 함수들

그냥 return 값 -1같이 해놓음
==========================================
*/

// 임시 버전: 자식 프로세스 생성 (fork)
tid_t fork(const char *name, struct intr_frame *f) {
    return -1;  // 아직 구현 안 됨
}

/*
===============================
[exec]
현재 프로세스를 커맨드라인에서
지정된 인수를 전달하여 이름이 
   지정된 실행 파일로 변경 
===============================
*/
int exec(const char *file_name) {

check_address(file_name);

int file_size =strlen(file_name) + 1; //NULL까지 + 1
char *fn_copy = palloc_get_page(PAL_ZERO);


if (fn_copy == NULL){
     exit(-1);
}
strlcpy(fn_copy,file_name,file_size);

if(process_exec(fn_copy)== -1){
     return -1;
}
NOT_REACHED();


return 0;
}


// 임시 버전: seek (커서 이동)
void seek(int fd, unsigned position) {
    return;
}

// 임시 버전: 현재 커서 위치
unsigned tell(int fd) {
    return 0;
}





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
	// printf ("system call!\n");
	// thread_exit ();

   int number =f->R.rax;

   /*
   =======================================
     시스템 콜 번호(rax 인자로 전달) 보고
           실제 커널 함수로 분기

   [ struct intr_fram *f ]

   유저 => 커널로 전환될 때 
   CPU의 레지스터,플래그,스택 포인터 등 
   프로세서 상태 전체를 보존해두는 구조체

   이 구조체의 주소가 전달된다. (포인터)
   =======================================
   */

   //char *fn_copy;
   /*
   =======================================
   시스템 콜 처리에서 파일명 같은 문자열을 
   임시로 복사 할 때 사용하는 포인터 변수 

   유저 영역에서 커널 영역으로 안전하게 복사

   복사본을 만들어서 커널 메모리 안에서 
   안전하게 사용한다. 
   =======================================
   */

   switch(number){ //시스템 콜 번호 인자 rax에 넘김
          case SYS_HALT:
               halt(); //pintos 자체를 (os)POWER-OFF
               break;
          case SYS_EXIT:
               exit(f->R.rdi);
               break; //현재 프로세스를 종료
          case SYS_FORK:
          /*
         ======================================================
         시스템 콜 핸들러에서 리턴값은 rax 레지스터에 저장돼야 한다.
         fork 함수의 리턴값을 f->R.rax에 저장하는 코드

         fork() 시스템 콜을 실행하고, 그 리턴값을 유저 prog에게 전달
         rdi는 첫번째 인자
         ======================================================
         */
              f->R.rax = fork(f->R.rdi, f);
         //f는 위에서 이 핸들러 함수에서 전달해준 프로세서 구조체
              break;

          case SYS_EXEC:
          /*
          =================================================
           -[f->R.rdi]: 유저 프로그램이 exec("file_name")으로 
            넘긴 첫 번째 인자(실행할 파일 이름)

           -exec(...) : 그 프로그램을 실행(로딩) 시도

           [실행 결과]
            * 0 또는 양수 : 성공적인 실행 (보통은 PID나 0)
            * -1 : 실패 (파일을 못 찾거나 , 잘못된 포맷)
          =================================================
          */
              f->R.rax =exec(f->R.rdi);
              break;
          case SYS_WAIT:
               f->R.rax =process_wait(f->R.rdi);
               //precess_wait 함수에 첫번째 인자 전달해주기
               break;

          case SYS_CREATE:
          /*
          =====================================================
          f->R.rdi : 첫 번째 인자 (파일 이름 , const char *file)
          f->R.rsi : 두 번째 인자 (초과 크기,unsigned initial_size)
          =====================================================
          */
               f->R.rax =create(f->R.rdi,f->R.rsi);
               break;
          case SYS_REMOVE:
               f->R.rax =remove(f->R.rdi);
               break;
          case SYS_OPEN: 
               f->R.rax =open(f->R.rdi);
               break; //와 이 미친것 여기 break;안해서 그런거였음 
          case SYS_FILESIZE:
               f->R.rax = filesize(f->R.rdi);
               break;
          case SYS_READ:
               f->R.rax = read(f->R.rdi,f->R.rsi,f->R.rdx);
               //첫번째 인자 두번째 인자 세번째 인자
               break; 
               /*
               ======================================
               int read(int fd ,void *buffer,unsined size);
               
               fd : 읽을 파일 디스크립터 (파일을 구분하는 숫자)
               buffer : 데이터를 읽어들일 메모리 공간
               size : 몇 바이트를 읽을 것인지 
               ======================================
               */

          case SYS_WRITE:
               f->R.rax = write(f->R.rdi, (const void *)f -> R.rsi, f->R.rdx);
               break;
              
          case SYS_SEEK:
          /*
          ==========================================
          반환할 값이 없나봐 

          "파일의 커서 위치를 바꾼다"는 역할만 하고 
          별도로 리턴값을 줄 필요는 없음 
          ==========================================
          */
               seek(f->R.rdi ,f->R.rsi);
               break;
          case SYS_TELL:
               f->R.rax = tell(f->R.rdi);
               break;
          case SYS_CLOSE:
               close(f->R.rdi);
               break;
          default:
               exit(-1);
               break;
   }
               
}






