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

void syscall_entry(void);
void syscall_handler(struct intr_frame *);
void check_address(void *addr);
void halt(void);
void exit(int status);
tid_t fork(const char *thread_name, struct intr_frame *f);
int exec(const *file);
int wait(tid_t tid);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
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

#define MSR_STAR 0xc0000081			/* Segment selector msr */
#define MSR_LSTAR 0xc0000082		/* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void syscall_init(void)
{
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48 |
							((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t)syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			  FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
	lock_init(&filesys_lock);
}

/* The main system call interface */
void syscall_handler(struct intr_frame *f UNUSED)
{
	uint32_t *sp = f->rsp; /* 유저 스택 포인터 */
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
	/* power_off()를 사용하여 pintos 종료 */
}

void exit(int status)
{
	struct thread *cur = thread_current();
	cur->exit_status = status;
	printf("%s: exit(%d)\n", cur->name, status);
	thread_exit();
}

int exec(const *file) // cmd_line: 새로운 프로세스에 실행할 프로그램 명령어
{
	check_address(file);
	int file_size = strlen(file) + 1;
	char *fn_copy = palloc_get_page(PAL_ZERO);
	if (fn_copy == NULL)
	{
		exit(-1);
	}
	strlcpy(fn_copy, file, file_size); // file 이름만 복사
	if (process_exec(fn_copy) == -1)
	{
		exit(-1);
	}
}

tid_t fork(const char *thread_name, struct intr_frame *f)
{
	return process_fork(thread_name, f);
}

int wait(tid_t pid)
{
	return process_wait(pid);
}

bool create(const char *file, unsigned initial_size)
{
	/*
	- 파일 생성하는 시스템 콜
	- 성공일 경우 true, 실패일 경우 false 리턴
	- file: 생성할 파일의 이름 및 경로 정보
	- initial_size: 생성할 파일 크기
	*/
	check_address(file);
	lock_acquire(&filesys_lock);
	bool success = filesys_create(file, initial_size);
	lock_release(&filesys_lock);
	return success;
}

bool remove(const char *file)
{
	/*
	- 파일을 삭제하는 시스템 콜
	- file : 제거할 파일의 이름 및 경로 정보
	- 성공 일 경우 true, 실패 일 경우 false 리턴
	*/
	check_address(file);
	lock_acquire(&filesys_lock);
	bool success = filesys_remove(file);
	lock_release(&filesys_lock);
	return success;
}

/*
 * 파일을 열고 해당 파일 객체에 파일 디스크립터 부여하고 파일 디스크립터 반환
 */
int open(const char *file)
{
	check_address(file);
	/* 파일을 open */
	lock_acquire(&filesys_lock);
	struct file *fileobj = filesys_open(file);
	if (is_dir(fileobj))
	{
		// thread_current()->dir = "asdf";
		return 0;
	}

	/* 해당 파일이 존재하지 않으면 -1 리턴 */
	if (fileobj == NULL)
	{
		lock_release(&filesys_lock);
		return -1;
	}
	/* 해당 파일 객체에 파일 디스크립터 부여 */
	int fd = process_add_file(fileobj);

	if (fd == -1)
	{
		file_close(fileobj);
	}
	/* 파일 디스크립터 리턴 */
	lock_release(&filesys_lock);
	return fd;
}

/*
 * fd에 해당하는 파일을 찾고 그 파일의 크기를 반환한다.
 */
int filesize(int fd)
{
	struct file *open_file = process_get_file(fd);
	if (open_file == NULL)
	{
		return -1;
	}
	lock_acquire(&filesys_lock);
	int length = file_length(open_file);
	lock_release(&filesys_lock);
	return length;
}
/*
 * fd를 이용해서 파일 객체를 검색하고 입력을 버퍼에 저장하고, 버퍼에 저장한 크기를 반환
 */
int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);
	struct page *page = spt_find_page(&thread_current()->spt, pg_round_down(buffer));
	if (page != NULL && page->writable == 0)
		exit(-1);
	off_t read_byte = 0;
	uint8_t *read_buffer = (char *)buffer;
	lock_acquire(&filesys_lock);
	if (fd == 0)
	{
		char key;
		for (read_byte = 0; read_byte < size; read_byte++)
		{
			key = input_getc();	  // 키보드에 한 문자 입력받기
			*read_buffer++ = key; // read_buffer에 받은 문자 저장
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

/*
 * fd에 해당하는 파일을 열어서 buffer에 저장된 데이터를 size만큼 파일에 기록 후 기록한 바이트 수 리턴하는 함수
 */
int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);
	struct file *write_file = process_get_file(fd);
	int bytes_write;
	lock_acquire(&filesys_lock);
	if (fd < 2)
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
		if (write_file == NULL)
		{
			lock_release(&filesys_lock);
			return -1;
		}
		bytes_write = file_write(write_file, buffer, size);
	}
	lock_release(&filesys_lock);
	return bytes_write;
}

/*
 * 파일에서 position으로 이동하는 함수
 */
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

/*
 * 열린 파일의 위치를 반환하는 함수
 */
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

/*
 * 파일 디스크립터 엔트리 초기화
 */
void close(int fd)
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
	lock_acquire(&filesys_lock);
	file_close(close_file);
	lock_release(&filesys_lock);
	process_close_file(fd);
}

void *mmap(void *addr, size_t length, int writable, int fd, off_t offset)
{
	if (pg_ofs(addr) != 0 || (uint64_t)addr <= 0 || is_kernel_vaddr(addr) || pg_ofs(offset) != 0)
		return NULL;
	if (is_kernel_vaddr((uint64_t)addr + length) || (uint64_t)addr + length <= 0)
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