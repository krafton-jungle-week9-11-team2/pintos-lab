#ifndef __LIB_SYSCALL_NR_H
#define __LIB_SYSCALL_NR_H

/* 
======================================================================
             System call numbers. 

 -열거형 (enum)으로 선언된 상수들은 0,1,2 .. 순으로 자동 지정
 -각 번호는 하나의 시스템 콜을 의미 
 -이 번호를 기반으로 syscall_handler()는 해당 기능을 디스패치만 하고
  실제 기능은 sys_write,sys_open 등의 별도의 함수에 구현됨

 [각 프로젝트별 구현 범위 정리]
 -SYS_HALT ~ SYS_CLOSE (Project 2) => 기본적인 파일, 프로세스, I/O
 -SYS_MMAP ~ SYS_CLOSE (Project 3) => 메모리 매핑
 -SYS_CHDIR ~ SYS_SYMLINK (Project 4) => 파일 시스템 관련 고급 기능
======================================================================


*/
enum {
	/* Projects 2 and later. */
	SYS_HALT,                   /* Halt the operating system. */
	SYS_EXIT,                   /* Terminate this process. */
	SYS_FORK,                   /* Clone current process. */
	SYS_EXEC,                   /* Switch current process. */
	SYS_WAIT,                   /* Wait for a child process to die. */
	SYS_CREATE,                 /* Create a file. */
	SYS_REMOVE,                 /* Delete a file. */
	SYS_OPEN,                   /* Open a file. */
	SYS_FILESIZE,               /* Obtain a file's size. */
	SYS_READ,                   /* Read from a file. */
	SYS_WRITE,                  /* Write to a file. */
	SYS_SEEK,                   /* Change position in a file. */
	SYS_TELL,                   /* Report current position in a file. */
	SYS_CLOSE,                  /* Close a file. */

	/* Project 3 and optionally project 4. */
	SYS_MMAP,                   /* Map a file into memory. */
	SYS_MUNMAP,                 /* Remove a memory mapping. */

	/* Project 4 only. */
	SYS_CHDIR,                  /* Change the current directory. */
	SYS_MKDIR,                  /* Create a directory. */
	SYS_READDIR,                /* Reads a directory entry. */
	SYS_ISDIR,                  /* Tests if a fd represents a directory. */
	SYS_INUMBER,                /* Returns the inode number for a fd. */
	SYS_SYMLINK,                /* Returns the inode number for a fd. */

	/* Extra for Project 2 */
	SYS_DUP2,                   /* Duplicate the file descriptor */

	SYS_MOUNT,
	SYS_UMOUNT,
};

#endif /* lib/syscall-nr.h */
