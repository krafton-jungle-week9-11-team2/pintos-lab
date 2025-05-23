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
#include "filesys/filesys.h"
#include "threads/palloc.h"
#include "threads/synch.h" // 🔥 struct lock 정의

/* 파일 시스템 락 */
extern struct lock filesys_lock;

/* syscall 관련 레지스터 설정용 MSR 번호 */
#define MSR_STAR 0xc0000081
#define MSR_LSTAR 0xc0000082
#define MSR_SYSCALL_MASK 0xc0000084

/* Forward declarations */
void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void check_address(const void *addr);

// 두 함수는 선언 필요 => 표준 라이브러리하고 겹침
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
/* 시스템 콜 초기화 함수 */
void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
													((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);
	write_msr(MSR_SYSCALL_MASK,
						FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* 유저가 넘겨준 주소가 유저 영역이고 매핑되어 있는지 확인 */
void check_address(const void *addr)
{
	struct thread *cur = thread_current();
	if (addr == NULL || !is_user_vaddr(addr) || pml4_get_page(cur->pml4, addr) == NULL)
		exit(-1);
}

/* 시스템 콜 핸들러: syscall 번호에 따라 함수 호출 */
void syscall_handler(struct intr_frame *f UNUSED)
{
	switch (f->R.rax)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(f->R.rdi);
		break;
	case SYS_FORK:
		f->R.rax = fork(f->R.rdi, f);
		break;
	case SYS_EXEC:
		f->R.rax = exec(f->R.rdi);
		break;
	case SYS_WRITE:
		f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	case SYS_READ:
		f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
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
	case SYS_WAIT:
		f->R.rax = wait(f->R.rdi);
		break;
	default:
		exit(-1);
		break;
	}
}

/* ===== 시스템 콜 구현부 ===== */

/* 시스템 종료 */
void halt(void)
{
	power_off();
}

/* 현재 프로세스 종료 */
void exit(int status)
{
	struct thread *cur = thread_current();
	cur->exit_status = status;
	printf("%s: exit(%d)\n", thread_name(), status);
	thread_exit();
}

/* 프로그램 실행 */
int exec(const char *cmd_line)
{
	check_address(cmd_line); // 유저 영역 포인터 확인

	char *cmd_line_copy = palloc_get_page(0);
	if (cmd_line_copy == NULL)
		exit(-1);

	strlcpy(cmd_line_copy, cmd_line, PGSIZE);

	if (process_exec(cmd_line_copy) == -1)
		exit(-1);

	NOT_REACHED();
}

/* 프로세스 복제 (fork) */
int fork(const char *thread_name, struct intr_frame *f)
{
	return process_fork(thread_name, f);
}

/* 파일 쓰기 */
int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);

	if (fd == 1)
	{
		putbuf(buffer, size);
		return size;
	}

	struct file *file = find_file_by_fd(fd);
	if (file == NULL)
		return -1;

	lock_acquire(&filesys_lock);
	int result = file_write(file, buffer, size);
	lock_release(&filesys_lock);

	return result;
}

int wait(int tid)
{
	return process_wait(tid);
}

/* 파일 읽기 */
int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);

	if (fd == 0)
	{
		for (unsigned i = 0; i < size; i++)
			((char *)buffer)[i] = input_getc();
		return size;
	}
	if (fd == 1)
		return -1;

	struct file *file = find_file_by_fd(fd);
	if (file == NULL)
		return -1;

	lock_acquire(&filesys_lock);
	int result = file_read(file, buffer, size);
	lock_release(&filesys_lock);

	return result;
}

/* 파일 생성 */
bool create(const char *file, unsigned initial_size)
{
	check_address(file);
	return filesys_create(file, initial_size);
}

/* 파일 삭제 */
bool remove(const char *file)
{
	check_address(file);
	return filesys_remove(file);
}

/* 파일 열기 */
int open(const char *file)
{
	check_address(file);
	struct file *f = filesys_open(file);
	if (f == NULL)
		return -1;

	int fd = add_file_to_fdt(f);
	if (fd == -1)
		file_close(f);
	return fd;
}

/* 파일 크기 반환 */
int filesize(int fd)
{
	struct file *file = find_file_by_fd(fd);
	if (file == NULL)
		return -1;
	return file_length(file);
}

/* 파일 닫기 */
void close(int fd)
{
	struct thread *curr = thread_current();

	// 🔒 fd_table이 NULL이면 아무 것도 하지 않음
	if (curr->fd_table == NULL)
		return;

	// 1. 유효한 fd 범위 확인
	if (fd < 0 || fd >= FDCOUNT_LIMIT)
		return;

	struct file *file = curr->fd_table[fd];
	if (file == NULL)
		return;

	// 2. file 자원 해제
	file_close(file);

	// 3. fd_table에서 NULL로 초기화
	curr->fd_table[fd] = NULL;
}

/* ===== 파일 디스크립터 테이블 관련 함수 ===== */

/* fd로 파일 구조체를 가져옴 */
struct file *find_file_by_fd(int fd)
{
	struct thread *cur = thread_current();
	if (fd < 0 || fd >= FDCOUNT_LIMIT)
		return NULL;
	return cur->fd_table[fd];
}

/* 파일을 fdt에 추가하고 fd 번호 반환 */
int add_file_to_fdt(struct file *file)
{
	struct thread *cur = thread_current();
	while (cur->fd_idx < FDCOUNT_LIMIT && cur->fd_table[cur->fd_idx])
		cur->fd_idx++;

	if (cur->fd_idx >= FDCOUNT_LIMIT)
		return -1;

	cur->fd_table[cur->fd_idx] = file;
	return cur->fd_idx;
}

/* fdt에서 fd 제거 및 파일 닫기 */
void remove_file_from_fdt(int fd)
{
	struct thread *cur = thread_current();
	if (fd < 2 || fd >= FDCOUNT_LIMIT)
		return;

	if (cur->fd_table[fd])
	{
		file_close(cur->fd_table[fd]);
		cur->fd_table[fd] = NULL;
	}
}
