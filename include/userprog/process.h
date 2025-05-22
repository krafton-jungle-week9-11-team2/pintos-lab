#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd (const char *file_name);
//초기 init 프로세스 생성
tid_t process_fork (const char *name, struct intr_frame *if_);
//현재 프로세스를 복제
int process_exec (void *f_name);
//현재 프로세스를 다른 유저 프로그램으로 덮어쓰기
int process_wait (tid_t);
//자식 프로세스가 종료될 때까지 대기
void process_exit (void);
//현재 프로세스를 종료
void process_activate (struct thread *next);
//프로세스 전환 시 활성화 작업
void argument_stack_user(char **argv,int argc, void **rsp);
int process_add_file(struct file *file_obj);



#endif /* userprog/process.h */
