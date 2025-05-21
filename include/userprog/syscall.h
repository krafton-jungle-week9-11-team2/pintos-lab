#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "filesys/file.h" // 꼭 포함되어야 함!
int process_wait(tid_t child_tid);
void syscall_init(void);
struct file *find_file_by_fd(int fd);
#endif /* userprog/syscall.h */
