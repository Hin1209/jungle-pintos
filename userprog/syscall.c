#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "userprog/process.h"
#include "threads/palloc.h"
#include "vm/file.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void check_address(void *addr);
void halt(void);
void exit(int status);
tid_t fork(const char *thread_name, struct intr_frame *f);
int exec(const *file) ;
int wait (tid_t tid);
bool create (const char *file , unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close (int fd);
void *mmap(void *addr, size_t length, int writable, int fd, off_t offset);
void munmap(void *addr);

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
	lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	uint32_t *sp = f -> rsp; 
	thread_current()->user_rsp = f->rsp;
	check_address((void *)sp);

	char *fn_copy;
	int siz;
	int a = f->R.rax;
	switch (a)
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
		if (exec(f->R.rdi) == -1)
			exit(-1);
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
		f->R.rax = open(f->R.rdi);
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
	case SYS_TELL:
		f->R.rax = tell(f->R.rdi);
		break;
	case SYS_CLOSE:
		close(f->R.rdi);
		break;

	case SYS_MMAP:
		f->R.rax = mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
		break;
	case SYS_MUNMAP:
		munmap(f->R.rdi);
		break;
	default:
		exit(-1);
		break;
	}
}

void check_address(void *addr)
{
	struct thread *curr = thread_current();
	if (addr == NULL || !(is_user_vaddr(addr)))
	{
		exit(-1);
	}
}

void halt(void)
{
	power_off();
}

void exit(int status)
{
	struct thread *cur = thread_current (); 
	cur->exit_status = status;
	printf("%s: exit(%d)\n" , cur->name , status);
	thread_exit();
}


int exec(const *file) 
{
	check_address(file);
	int file_size = strlen(file) + 1;
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy == NULL)
	{
			exit(-1);
	}
	strlcpy(fn_copy, file, file_size);
	if (process_exec(fn_copy) == -1)
	{
			exit(-1);
	}
}

tid_t fork(const char *thread_name, struct intr_frame *f) {
	return process_fork(thread_name, f);
}


int wait (tid_t pid)
{
	return process_wait(pid);
}

bool create (const char *file , unsigned initial_size)
{
	check_address(file);
	return filesys_create(file, initial_size);
}

bool remove (const char *file)
{
	bool return_code;
	check_address(file);
	lock_acquire(&filesys_lock);
	return_code = filesys_remove(file);
	lock_release(&filesys_lock);
	return return_code;
}

int open (const char *file)
{
	check_address(file);
	struct file *fileobj = filesys_open(file);

	if (fileobj == NULL)
	{
		return -1;
	}
	int fd = process_add_file(fileobj);

	if (fd == -1)
	{
		file_close(fileobj);
	}

	return fd;
}

int filesize(int fd)
{
	struct file *open_file = process_get_file(fd);
	if(open_file == NULL)
	{
		return -1;
	}
	return file_length(open_file);
}
int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);
	struct page *page = spt_find_page(&thread_current()->spt, pg_round_down(buffer));
	if(page != NULL && page->writable == 0){
		exit(-1);
	}
	off_t read_byte = 0;
	uint8_t *read_buffer = (char *)buffer;
	lock_acquire(&filesys_lock);
	if (fd == 0)
	{
		char key;
		for (read_byte = 0; read_byte < size; read_byte++)
		{
			key = input_getc();
			*read_buffer++ = key;
			if (key == '\n')
			{
				break;
			}
		}
	}
	else if (fd == 1)
	{
		lock_release(&filesys_lock);
		return -1;
	}
	else
	{
		struct file *read_file = process_get_file(fd);
		if (read_file == NULL)
		{
			lock_release(&filesys_lock);
			return -1;
		}
		read_byte = file_read(read_file, buffer, size);
	}
	lock_release(&filesys_lock);
	return read_byte;
}


int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);
	struct file *write_file = process_get_file(fd);
	int bytes_write;
	lock_acquire(&filesys_lock);
	if(fd < 2)
	{
		if (fd == 1)
		{
			putbuf(buffer, size);
			bytes_write = size;
			lock_release(&filesys_lock);
			return size;
		}
		lock_release(&filesys_lock);
		return -1;
	}
	else
	{
		if (write_file == NULL){
			lock_release(&filesys_lock);
			return -1;
		}
		bytes_write = file_write(write_file, buffer, size);
	}
	lock_release(&filesys_lock);
	return bytes_write;
}


void seek(int fd, unsigned position)
{
	struct file *seek_file = process_get_file(fd);
	if (fd < 2)
	{
		return;
	}
	if (seek_file == NULL)
	{
		return;
	}
	file_seek(seek_file, position);
}

unsigned tell(int fd)
{
	struct file *tell_file = process_get_file(fd);
	if (fd < 2)
	{
		return;
	}
	if (tell_file == NULL)
	{
		return;
	}
	return file_tell(tell_file);
}

void close (int fd)
{
	struct file *close_file = process_get_file(fd);
	if (fd < 2)
	{
		return;
	}
	if (close_file == NULL)
	{
		return;
	}
	file_close(close_file);
	process_close_file(fd);
}

void *mmap(void *addr, size_t length, int writable, int fd, off_t offset)
{
	if (pg_ofs(addr) != 0 || (uint64_t)addr <= 0 || is_kernel_vaddr(addr))
		return NULL;
	if (is_kernel_vaddr((uint64_t)addr + length) || (uint64_t)addr + length <= 0 || pg_ofs(offset) != 0)
		return NULL;
	struct file *file = process_get_file(fd);
	if (file == NULL || length == 0)
		return NULL;
	return do_mmap(addr, length, writable, file, offset);
}

void munmap(void *addr)
{
	check_address(addr);
	do_munmap(addr);
}
