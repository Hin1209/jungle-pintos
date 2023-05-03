#include "userprog/syscall.h"
#include <stdio.h>
#include <stdlib.h>
#include <syscall-nr.h>
#include <filesys/filesys.h>
#include <filesys/file.h>
#include <devices/input.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void halt(void);
void exit(int status);
int exec(const char *cmd_line);
bool create(const char *file, unsigned int initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned int size);
int write(int fd, void *buffer, unsigned int size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
void check_address(void *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081			/* Segment selector msr */
#define MSR_LSTAR 0xc0000082		/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
							((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);
	lock_init(&filesys_lock);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	uint64_t num = f->R.rax;
	uint64_t arg1 = f->R.rdi;
	uint64_t arg2 = f->R.rsi;
	uint64_t arg3 = f->R.rdx;
	uint64_t arg4 = f->R.r10;
	uint64_t arg5 = f->R.r8;
	uint64_t arg6 = f->R.r9;
	// TODO: Your implementation goes here.
	switch (f->R.rax)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(arg1);
		break;
	case SYS_FORK:
		check_address(arg1);
		fork(arg1);
		break;
	case SYS_EXEC:
		check_address(arg1);
		exec(arg1);
		break;
	case SYS_WAIT:
		check_address(arg1);
		wait(arg1);
		break;
	case SYS_CREATE:
		check_address(arg1);
		f->R.rax = create(arg1, arg2);
		break;
	case SYS_REMOVE:
		check_address(arg1);
		f->R.rax = remove(arg1);
		break;
	case SYS_OPEN:
		check_address(arg1);
		f->R.rax = open(arg1);
		break;
	case SYS_FILESIZE:
		f->R.rax = filesize(arg1);
		break;
	case SYS_READ:
		check_address(arg2);
		f->R.rax = read(arg1, arg2, arg3);
		break;
	case SYS_WRITE:
		check_address(arg2);
		f->R.rax = write(arg1, arg2, arg3);
		break;
	case SYS_SEEK:
		seek(arg1, arg2);
		break;
	case SYS_TELL:
		f->R.rax = tell(arg1);
		break;
	case SYS_CLOSE:
		close(arg1);
		break;
	case SYS_DUP2:
		break;
	}
	// thread_exit();
}

/*
 * Halting the OS
 */
void halt(void)
{
	/* pintOS 종료 */
	power_off();
}

/*
 * Terminating this process
 */
void exit(int status)
{
	/* 실행 중인 스레드 구조체 가져오기 */
	struct thread *cur = thread_current();

	/* 프로세스 종료 메시지 출력하기  */
	printf("%s: exit(%d)\n", cur->name, status);

	/* 스레드 종료 */
	thread_exit();
}

tid_t fork(const char *name, struct intr_frame *if_ UNUSED)
{
	process_fork(name, if_);
}

int exec(const char *cmd_line)
{
	/* 자식 프로세스 생성 */
	if (process_exec(cmd_line) == -1)
		return -1;
	return 0;
}

/*
* 자식 프로세스 pid를 wait, 그것의 종료 상태를 검색
*/
int wait(tid_t pid)
{
	struct thread *cur = thread_current();
	struct thread *child = NULL;
	struct list_elem *e = NULL;

	/* 주어진 pid를 가진 자식 스레드 찾기 */
	// for (e = list_begin(&cur->child_list); e != list_end(&cur->child_list); e = list_next(e))
	// {
	// 	child = list_entry(e, struct thread, child_elem);
	// 	if (child->tid == pid)
	// 		break;
	// 	else
	// 		child = NULL;
	// }
	child = get_child_process(pid);

	/* 자식 못 찾으면, -1 즉시 반환 */
	if (child == NULL)
		return -1;

	/* 자식 종료까지 wait */
	sema_down(&child->exit_sema);

	/* 자식의 종료 상태 검색 */
	int exit_status = child->terminated_status;
	/* 자식 리스트에서 자식 제거 & 자식 자원 메모리 해제 -> remove_child_process 구현 필요 */
	remove_child_process(&(child->child_elem));

	return exit_status;	
}

bool create(const char *file, unsigned int initial_size)
{
	lock_acquire(&filesys_lock);
	bool file_create = filesys_create(file, initial_size);

	if (file_create)
	{
		lock_release(&filesys_lock);
		return true;
	}
	else
	{
		lock_release(&filesys_lock);
		return false;
	}
}

bool remove(const char *file)
{
	lock_acquire(&filesys_lock);
	bool file_remove = filesys_remove(file);
	if (file_remove)
	{
		lock_release(&filesys_lock);
		return true;
	}
	else
	{
		lock_release(&filesys_lock);
		return false;
	}
}

int open(const char *file)
{
	lock_acquire(&filesys_lock);
	struct thread *curr = thread_current();
	struct file *open_file = filesys_open(file);
	int fd;
	if (open_file != NULL)
	{
		for (int i = 0; i < 64; i++)
		{
			if (curr->file_list[i] == open_file)
			{
				lock_release(&filesys_lock);
				return i;
			}
		}
		curr->file_list[curr->file_descriptor] = open_file;
		fd = curr->file_descriptor;
		for (int i = fd + 1; i < 64; i++)
		{
			if (curr->file_list[i] == NULL)
			{
				curr->file_descriptor = i;
				break;
			}
		}
		lock_release(&filesys_lock);
		return fd;
	}
	lock_release(&filesys_lock);
	return -1;
}

int filesize(int fd)
{
	if (fd < 0 || fd >= 64)
		return -1;
	lock_acquire(&filesys_lock);
	struct thread *curr = thread_current();
	struct file *file = curr->file_list[fd];
	if (file == NULL)
	{
		lock_release(&filesys_lock);
		return -1;
	}
	else
	{
		lock_release(&filesys_lock);
		return file_length(file);
	}
}

int read(int fd, void *buffer, unsigned int size)
{
	if (fd < 0 || fd >= 64)
		return -1;
	int readn = 0;
	lock_acquire(&filesys_lock);
	struct thread *curr = thread_current();
	struct file *file = curr->file_list[fd];
	char tmp;
	if (fd >= 2)
	{
		if (file == NULL)
		{
			lock_release(&filesys_lock);
			return -1;
		}
		if (size == 0)
		{
			lock_release(&filesys_lock);
			return 0;
		}
		readn = file_read(file, buffer, size);
	}
	else if (fd == 0)
	{
		if (size == 0)
		{
			lock_release(&filesys_lock);
			return 0;
		}
		for (unsigned int i = 0; i < size; i++)
		{
			*(char *)(buffer + i) = input_getc();
			readn += 1;
		}
	}
	lock_release(&filesys_lock);
	return readn;
}

int write(int fd, void *buffer, unsigned int size)
{
	if (fd < 0 || fd >= 64)
		return -1;
	int writen = 0;
	lock_acquire(&filesys_lock);
	struct thread *curr = thread_current();
	struct file *file = curr->file_list[fd];
	if (fd >= 2)
	{
		if (file == NULL)
		{
			lock_release(&filesys_lock);
			return -1;
		}
		writen = file_write(file, buffer, size);
	}
	else if (fd == 1)
	{
		putbuf(buffer, size);
		writen = size;
	}
	lock_release(&filesys_lock);
	return writen;
}

void seek(int fd, unsigned position)
{
	if (fd < 0 || fd >= 64)
		return -1;
	lock_acquire(&filesys_lock);
	struct thread *curr = thread_current();
	struct file *file = curr->file_list[fd];
	if (file != NULL)
		file_seek(file, position);
	lock_release(&filesys_lock);
}

unsigned tell(int fd)
{
	if (fd < 0 || fd >= 64)
		return -1;
	lock_acquire(&filesys_lock);
	struct thread *curr = thread_current();
	struct file *file = curr->file_list[fd];
	if (file != NULL)
	{
		lock_release(&filesys_lock);
		return file_tell(file);
	}
	else
	{
		lock_release(&filesys_lock);
		return -1;
	}
}

void close(int fd)
{
	if (fd < 0 || fd >= 64)
		return -1;
	lock_acquire(&filesys_lock);
	struct thread *curr = thread_current();
	struct file *file = curr->file_list[fd];
	if (file != NULL)
	{
		curr->file_list[fd] = NULL;
		if (fd < curr->file_descriptor)
			curr->file_descriptor = fd;
		file_close(file);
	}
	lock_release(&filesys_lock);
}

void check_address(void *address)
{
	struct thread *curr = thread_current();
	if (address == NULL || is_kernel_vaddr(address) || pml4_get_page(curr->pml4, address) == NULL)
		exit(-1);
}
