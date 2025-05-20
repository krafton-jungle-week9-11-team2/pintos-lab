#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include <syscall-nr.h>
#include "filesys/filesys.h"
#include "threads/synch.h" // 이게 있어야 struct lock 인식 가능
#include "threads/palloc.h"

extern struct lock filesys_lock;

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void halt(void) NO_RETURN;

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081					/* Segment selector msr */
#define MSR_LSTAR 0xc0000082				/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
	// intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
													((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
						FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */

void check_address(const uint64_t *addr)
{
	struct thread *cur = thread_current();
	if (addr == NULL || !(is_user_vaddr(addr)) || pml4_get_page(cur->pml4, addr) == NULL)
		exit(-1);
}

void halt(void)
{
	power_off();
}

/* 현재 프로세스를 종료시키는 시스템 콜*/
// 프로세스 종료 시스템 콜
void exit(int status)
{
	struct thread *cur = thread_current();
	cur->exit_status = status; // 프로그램이 정상적으로 종료되었는지 확인(정상적 종료 시 0)

	printf("%s: exit(%d)\n", thread_name(), status); // 종료 시 Process Termination Message 출력
	thread_exit();																	 // 스레드 종료
}

int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer); // 주소 유효성 검사

	if (fd == 1)
	{
		putbuf(buffer, size);
		return size;
	}

	return -1;
}
int exec(char *file_name)
{
	check_address(file_name);

	int file_size = strlen(file_name) + 1;
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy == NULL)
	{
		exit(-1);
	}
	strlcpy(fn_copy, file_name, file_size);

	if (process_exec(fn_copy) == -1)
	{
		return -1;
	}

	NOT_REACHED();
	return 0;
}

bool create(const char *file, unsigned initial_size)
{ // file: 생성할 파일의 이름 및 경로 정보, initial_size: 생성할 파일의 크기
	check_address(file);
	return filesys_create(file, initial_size);
}

// 파일 삭제하는 시스템 콜
// 성공일 경우 true, 실패일 경우 false 리턴
bool remove(const char *file)
{ // file: 제거할 파일의 이름 및 경로 정보
	check_address(file);
	return filesys_remove(file);
}
static struct file *find_file_by_fd(int fd)
{
	struct thread *cur = thread_current();

	if (fd < 0 || fd >= FDCOUNT_LIMIT)
	{
		return NULL;
	}
	return cur->fd_table[fd];
}
// fd인자를 받아 파일 크기 리턴
int filesize(int fd)
{
	struct file *open_file = find_file_by_fd(fd);
	if (open_file == NULL)
	{
		return -1;
	}
	return file_length(open_file);
}
void remove_file_from_fdt(int fd)
{
	struct thread *cur = thread_current(); // 현재 스레드 가져오기

	// fd가 유효한지 확인
	if (fd < 2 || fd >= FDCOUNT_LIMIT)
	{
		return;
	}

	// fdt에서 해당 fd 위치에 파일이 있는지 확인
	if (cur->fdt[fd] != NULL)
	{
		file_close(cur->fdt[fd]); // 열려 있는 파일 닫기
		cur->fdt[fd] = NULL;			// FDT에서 제거
	}
}
void close(int fd)
{
	struct file *fileobj = find_file_by_fd(fd);
	if (fileobj == NULL)
	{
		return;
	}
	remove_file_from_fdt(fd);
}
int add_file_to_fdt(struct file *file)
{
	struct thread *cur = thread_current();
	struct file **fdt = cur->fd_table;

	// fd의 위치가 제한 범위를 넘지 않고, fdtable의 인덱스 위치와 일치한다면
	while (cur->fd_idx < FDCOUNT_LIMIT && fdt[cur->fd_idx])
	{
		cur->fd_idx++;
	}

	// fdt이 가득 찼다면
	if (cur->fd_idx >= FDCOUNT_LIMIT)
		return -1;

	fdt[cur->fd_idx] = file;
	return cur->fd_idx;
}
int open(const char *file)
{
	check_address(file);
	struct file *open_file = filesys_open(file);

	if (open_file == NULL)
	{
		return -1;
	}

	int fd = add_file_to_fdt(open_file);

	// fd table 가득 찼다면
	if (fd == -1)
	{
		file_close(open_file);
	}
	return fd;
}

void syscall_handler(struct intr_frame *f UNUSED)
{
	uint64_t number = f->R.rax;

	switch (number)
	{
	case SYS_HALT:
		halt();
		// NOT_REACHED();
		break;

	case SYS_EXIT:
		exit(f->R.rdi);
		// NOT_REACHED();
		break;
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_CREATE:
		f->R.rax = create(f->R.rdi, f->R.rsi);
		break;
	case SYS_REMOVE:
		f->R.rax = remove(f->R.rdi);
		break;
	case SYS_OPEN:
		f->R.rax = open(f->R.rdi);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize(f->R.rdi);
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
		break;

	default:
		exit(-1);
		// NOT_REACHED();
		break;
	}
}

// 유저가 건네준 메모리 주소가 유효한지를 판별하는 함수 check_address
