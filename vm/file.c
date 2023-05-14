/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "userprog/process.h"

static bool file_backed_swap_in(struct page *page, void *kva);
static bool file_backed_swap_out(struct page *page);
static void file_backed_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void vm_file_init(void)
{
	//파일 지원 페이지 하위 시스템을 초기화합니다. 이 기능에서는 파일 백업 페이지와 관련된 모든 것을 설정할 수 있습니다.
}

/* Initialize the file backed page */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva)
{
	//파일 지원 페이지를 초기화합니다. 이 함수는 먼저 page->operations에서 파일 지원 페이지에 대한 핸들러를 설정합니다.
	//메모리를 지원하는 파일과 같은 페이지 구조에 대한 일부 정보를 업데이트할 수 있습니다.
	/* Set up the handler */
	page->operations = &file_ops;
	struct file_load *aux = page->uninit.aux;
	struct file_page *file_page = &page->file;
	file_page->file = aux->file;
	file_page->ofs = aux->ofs;
	file_page->read_bytes = aux->read_bytes;
	file_page->zero_bytes = aux->zero_bytes;
	file_page->file_length = aux->file_length;
	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in(struct page *page, void *kva)
{
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out(struct page *page)
{
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy(struct page *page)
{
	// 관련 파일을 닫아 파일 지원 페이지를 파괴합니다.
	// 내용이 dirty인 경우 변경 사항을 파일에 다시 기록해야 합니다.
	// 이 함수에서 페이지 구조를 free할 필요는 없습니다. file_backed_destroy의 호출자는 이를 처리해야 합니다.
	struct file_page *file_page UNUSED = &page->file;
}

static bool lazy_load(struct page *page, void *aux_)
{
	struct file_load *aux = (struct file_load *)aux_;
	struct file *file = aux->file;
	off_t ofs = aux->ofs;
	uint32_t read_bytes = aux->read_bytes;
	uint32_t zero_bytes = aux->zero_bytes;
	free(aux);

	file_seek(file, ofs);

	file_read(file, page->va, read_bytes);
	memset(page->va + read_bytes, 0, zero_bytes);
	return true;
}


/* Do the mmap */
void *
do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset)
{
	//bool valid = true;
	//한 페이지보다 클 수도 있고, 가상 공간 없어서 못 줄 수도 있다.
	//addr~length까지 할당가능한지 체크 
	int cnt_page = length % PGSIZE ? length / PGSIZE + 1 : length / PGSIZE;
	size_t length_ = length;

	for (int i =0; i < cnt_page; i++) {
		if(spt_find_page(&thread_current()->spt, addr + i * PGSIZE) != NULL){
			return NULL;
		}
	}

	for (int i = 0; i< cnt_page; i++) {
		struct file *file_ = file_reopen(file);
		struct file_load *aux = malloc(sizeof(struct file_load));
		aux->file_length = length;
		aux->file = file_;
		aux->ofs = offset + i * PGSIZE;
		if (length_ >= PGSIZE) {
			aux->read_bytes = PGSIZE;
			length_ -= PGSIZE;
		}
		else
			aux->read_bytes = length_;
		aux->zero_bytes = PGSIZE - aux->read_bytes;
		vm_alloc_page_with_initializer(VM_FILE, addr + i * PGSIZE, writable, lazy_load,aux);
	}
}

/* Do the munmap */
void do_munmap(void *addr)
{
	//지정된 주소 범위 addr에 대한 매핑을 해제합니다.
    //지정된 주소는 아직 매핑 해제되지 않은 동일한 프로세서의 mmap에 대한 이전 호출에서 반환된 가상 주소여야 합니다.
}
