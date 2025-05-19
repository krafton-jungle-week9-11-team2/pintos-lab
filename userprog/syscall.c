#include "filesys/filesys.h"
#include "userprog/process.h"

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
bool create(const char *file, unsigned initial_size);
bool remove(const char *file) ;
void check_address(const uint64_t *addr);

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


// // fd로 파일 찾는 함수
// static struct file *find_file_by_fd(int fd) {
// 	struct thread *cur = thread_current();

// 	if (fd < 0 || fd >= FDCOUNT_LIMIT) {
// 		return NULL;
// 	}
// 	return cur->fd_table[fd];
// }
 
// /**
//  * add_file_to_fdt - 현재 프로세스의 fd테이블에 파일 추가
//  * 
//  * @param file: 파일 파라미터
//  */
// int add_file_to_fdt(struct file *file) {
// 	struct thread *curr = thread_current();
// 	struct file **fdt = curr->fd_table;

// 	// limit을 넘지 않는 범위 안에서 빈 자리 탐색
// 	while (curr->next_fd < FDCOUNT_LIMIT && fdt[curr->next_fd])
// 		curr->next_fd++;
// 	if (curr->next_fd >= FDCOUNT_LIMIT)
// 		return -1;
// 	fdt[curr->next_fd] = file;

// 	return curr->next_fd;
// }

/**
 * seek - 파일을 찾음.
 * 
 * @param fd: 파일 디스크립터.
 * @param position: 위치.
 */
void seek(int fd, unsigned position)
{
	struct file *file = process_get_file_by_fd(fd);
	if (file == NULL)
		return;
	file_seek(file, position);
}

/**
 * exit - 해당 프로세스를 종료시킴.
 * 
 * @param status: 현재 상태를 가져오는 파라미터.
 */
void exit(int status) {
	struct thread *curr = thread_current();
    curr->exit_status = status;		// 프로그램이 정상적으로 종료되었는지 확인(정상적 종료 시 0)

	printf("%s: exit(%d)\n", thread_name(), status); 	// 디버그: 프로세스가 꺼진다는 Message 출력
	thread_exit();		// 스레드 종료
}

/**
 * halt - 머신을 halt함.
 * 
 * 
 */
void halt(void){
	// printf("NOW HALTING!\n"); 	// 종료 시 Process Termination Message 출력
	// for(int i=0;i<100000000;i++)
	// 	timer_msleep(2);
    power_off();
}


/**
 * write - fd에 *buffer로부터 size만큼을 작성.
 * 
 */
int write(int fd, const void *buffer, unsigned size){
	check_address(buffer);
	int bytes_write = 0;
	if (fd == STDOUT_FILENO)
	{
		putbuf(buffer, size);
		bytes_write = size;
	}
	else
	{
		if (fd < 2)
			return -1;
		struct file *file = process_get_file_by_fd(fd);
		if (file == NULL)
			return -1;
		lock_acquire(&filesys_lock);
		bytes_write = file_write(file, buffer, size);
		lock_release(&filesys_lock);
	}
	return bytes_write;
}


/**
 * exec - 현재 프로세스를 cmd_line에서 지정된 인수를 전달하여 이름이 지정된 실행 파일로 변경
 * 
 */
int exec(char *file_name) {
	check_address(file_name);

	int file_size = strlen(file_name)+1;
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy == NULL) {
		exit(-1);
	}
	strlcpy(fn_copy, file_name, file_size);

	if (process_exec(fn_copy) == -1) {
		return -1;
	}

	NOT_REACHED();
	return 0;
}

/**
 * 주소값이 유저 영역(0x8048000~0xc0000000)에서 사용하는 주소값인지 확인하는 함수
 */
void check_address(const uint64_t *addr){
	struct thread *cur = thread_current();
	if (addr == NULL || !(is_user_vaddr(addr)) || pml4_get_page(cur->pml4, addr) == NULL) 
		exit(-1);
}

/**
 * create - 파일을 생성.
 * 성공일 경우 true, 실패일 경우 false.
 * 
 * @param file: 생성할 파일의 이름 및 경로 정보
 * @param initial_size: 생성할 파일의 크기
 */
bool create(const char *file, unsigned initial_size) {		
	check_address(file);
	return filesys_create(file, initial_size);
}

/**
 * create - 파일을 삭제.
 * 성공일 경우 true, 실패일 경우 false.
 * 
 * @param file: 삭제할 파일의 이름 및 경로 정보
 * @param initial_size: 생성할 파일의 크기
 */
bool remove(const char *file) {	
	check_address(file);
	return filesys_remove(file);
}
 
/**
 * open - 파일을 오픈.
 * 성공일 경우 fd, 실패일 경우 -1.
 * 
 * @param file: 오픈할 파일의 이름 및 경로 정보
 */
int open(const char *filename) {
	check_address(filename); // 이상한 포인터면 즉시 종료
	struct file *file_obj = filesys_open(filename);
	
	if (file_obj == NULL) {
		return -1;
	}
	int fd = process_add_file(file_obj);

	if (fd == -1) { // fd table 꽉찬 경우 그냥 닫아버림
		file_close(file_obj);
    	file_obj = NULL;
	}

	return fd;
}

void close(int fd){
	struct file *file_obj = process_get_file_by_fd(fd);
	if (file_obj == NULL)
		return;
	file_close(file_obj);
	process_close_file_by_id(fd);
}

// 파일의 크기를 알려주는 시스템 콜
// fd인자를 받아 파일 크기 리턴
int filesize(int fd) {
	struct file *open_file = process_get_file_by_fd(fd);
	if (open_file == NULL) {
		return -1;
	}
	return file_length(open_file);
}

int read(int fd, void *buffer, unsigned size){
    if (size == 0)
        return 0;

    if (buffer == NULL || !is_user_vaddr(buffer))
        exit(-1);

	check_address(buffer);

	char *ptr = (char *)buffer;
	int bytes_read = 0;

	lock_acquire(&filesys_lock);
	if (fd == STDIN_FILENO)	{
		for (int i = 0; i < size; i++){
			*ptr++ = input_getc();
			bytes_read++;
		}
		lock_release(&filesys_lock);
	}else{
		if (fd < 2){
			lock_release(&filesys_lock);
			return -1;
		}
		struct file *file = process_get_file_by_fd(fd);
		if (file == NULL){
			lock_release(&filesys_lock);
			return -1;
		}
		bytes_read = file_read(file, buffer, size);
		lock_release(&filesys_lock);
	}
	return bytes_read;
}


void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	// printf ("system call!\n");
	int syscall_n = (int) f->R.rax; /* 시스템 콜 넘버 */

	/*
	 x86-64 규약은 함수가 리턴하는 값을 rax 레지스터에 배치하는 것
	 값을 반환하는 시스템 콜은 intr_frame 구조체의 rax 멤버 수정으로 가능
	 */
	// printf("%d, %d, %d, %d\n",SYS_HALT, SYS_EXIT, SYS_READ, syscall_n);
	switch (syscall_n) {		//  system call number가 rax에 있음.
		case SYS_HALT:
			halt();			// pintos를 종료시키는 시스템 콜
			break;
		case SYS_EXIT:
			exit(f->R.rdi);	// 현재 프로세스를 종료시키는 시스템 콜
			break;
		// case SYS_FORK:
		// 	f->R.rax = fork(f->R.rdi, f);
		// 	break;
		case SYS_EXEC:
			f->R.rax = exec(f->R.rdi);
       		break;
		case SYS_WAIT:
			f->R.rax = process_wait(f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
			break;
		case SYS_OPEN:
    		f->R.rax = open((const char *)f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);
			break;
		// case SYS_TELL:
		// 	f->R.rax = tell(f->R.rdi);
		// 	break;
		case SYS_CLOSE:
			close(f->R.rdi);
			break;
		default:
			printf("UNDEFINED SYSTEM CALL!, %d", syscall_n);
			exit(-1);
			break;
	}
	// thread_exit ();
}
