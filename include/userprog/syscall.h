#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "filesys/file.h" // 꼭 포함되어야 함!

void syscall_init(void);
struct file *find_file_by_fd(int fd);
#endif /* userprog/syscall.h */
