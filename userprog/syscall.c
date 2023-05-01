#include "userprog/syscall.h"
#include <stdio.h>
#include <stdlib.h>
#include <syscall-nr.h>
#include <filesys/filesys.h>
#include <filesys/file.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void halt(void);
void exit(int);
int exec(const char *);
int wait(pid_t);
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
	printf("system call!\n");
	switch (f->R.rax)
	{
	case SYS_HALT:
		halt();
		break;
	case SYS_EXIT:
		exit(arg1);
		break;
	case SYS_FORK:
		break;
	case SYS_EXEC:
		break;
	case SYS_WAIT:
		break;
	case SYS_CREATE:
		break;
	case SYS_REMOVE:
		break;
	case SYS_OPEN:
		check_address(arg1);
		f->R.rax = open(arg1);
		break;
	case SYS_FILESIZE:
		break;
	case SYS_READ:
		break;
	case SYS_WRITE:
		check_address(arg2);
		f->R.rax = write(arg1, arg2, arg3);
		break;
	case SYS_SEEK:
		break;
	case SYS_TELL:
		break;
	case SYS_CLOSE:
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
	struct thread *cur = thread_current;

	/* 프로세스 종료 메시지 출력하기  */
	printf("%s: exit (%d)\n", cur->name, status);

	/* 스레드 종료 */
	thread_exit();
}

int exec(const char *file)
{
	
}

int wait(pid_t pid)
{

}


int open(const char *file)
{
	struct thread *curr = thread_current();
	struct file *open_file = filesys_open(file);
	int fd;
	if (open_file != NULL)
	{
		curr->file_list[curr->file_descriptor] = open_file;
		fd = curr->file_descriptor++;
		return fd;
	}
	return NULL;
}

int write(int fd, void *buffer, unsigned size)
{
	enum intr_level old_level = intr_disable();
	int writen = 0;
	struct thread *curr = thread_current();
	lock_acquire(&filesys_lock);
	struct file *file = curr->file_list[fd];
	if (fd >= 2)
	{
		writen = file_write(file, buffer, size);
	}
	else if (fd == 1)
	{
		writen = puts(buffer);
	}
	lock_release(&filesys_lock);
	intr_set_level(old_level);
	return writen;
}

void check_address(void *address)
{
	struct thread *curr = thread_current();
	if (address == NULL || is_kernel_vaddr(address) || pml4_get_page(curr->pml4, address) == NULL)
		exit(-1);
}