/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "userprog/process.h"
#include "userprog/syscall.h"

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
}

/* Initialize the file backed page */
bool file_backed_initializer(struct page *page, enum vm_type type, void *kva)
{
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
	struct file_page *file_page UNUSED = &page->file;
	page->frame->cnt_page -= 1;
	bool is_lock_held = lock_held_by_current_thread(&filesys_lock);
	if (!is_lock_held)
		lock_acquire(&filesys_lock);
	if (pml4_is_dirty(thread_current()->pml4, page->va))
	{
		file_write_at(page->file.file, page->va, page->file.read_bytes, page->file.ofs);
	}
	file_close(page->file.file);
	if (!is_lock_held)
		lock_release(&filesys_lock);
	if (page->frame->cnt_page > 0)
		pml4_clear_page(thread_current()->pml4, page->va);
}

static bool lazy_load(struct page *page, void *aux_)
{
	struct file_load *aux = (struct file_load *)aux_;
	struct file *file = aux->file;
	off_t ofs = aux->ofs;
	uint32_t read_bytes = aux->read_bytes;
	uint32_t zero_bytes = aux->zero_bytes;
	free(aux);
	bool is_lock_held = lock_held_by_current_thread(&filesys_lock);
	if (!is_lock_held)
		lock_acquire(&filesys_lock);
	file_seek(file, ofs);
	read_bytes = file_read(file, page->frame->kva, read_bytes);
	if (!is_lock_held)
		lock_release(&filesys_lock);

	memset(page->frame->kva + read_bytes, 0, zero_bytes);
	return true;
}

/* Do the mmap */
void *
do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset)
{
	int cnt_page = length % PGSIZE ? length / PGSIZE + 1 : length / PGSIZE;
	size_t length_ = length;
	off_t ofs = file_length(file);
	if (ofs < offset)
		return NULL;
	for (int i = 0; i < cnt_page; i++)
	{
		if (spt_find_page(&thread_current()->spt, addr + i * PGSIZE) != NULL)
		{
			return NULL;
		}
	}
	bool is_lock_held = lock_held_by_current_thread(&filesys_lock);
	if (!is_lock_held)
		lock_acquire(&filesys_lock);
	for (int i = 0; i < cnt_page; i++)
	{
		struct file *file_ = file_reopen(file);
		struct file_load *aux = malloc(sizeof(struct file_load));
		aux->file_length = length;
		aux->file = file_;
		aux->ofs = offset + i * PGSIZE;
		if (length_ >= PGSIZE)
		{
			aux->read_bytes = PGSIZE;
			length_ -= PGSIZE;
		}
		else
			aux->read_bytes = length_;
		aux->zero_bytes = PGSIZE - aux->read_bytes;

		vm_alloc_page_with_initializer(VM_FILE, addr + i * PGSIZE, writable, lazy_load, aux);
	}
	if (!is_lock_held)
		lock_release(&filesys_lock);

	return addr;
}

/* Do the munmap */
void do_munmap(void *addr)
{
	size_t length;
	int cnt_page;
	if (pg_ofs(addr) != 0)
		return;
	struct page *page = spt_find_page(&thread_current()->spt, addr);
	if (page == NULL)
		return;
	length = page->file.file_length;
	cnt_page = length % PGSIZE ? length / PGSIZE + 1 : length / PGSIZE;
	bool is_lock_held = lock_held_by_current_thread(&filesys_lock);
	if (!is_lock_held)
		lock_acquire(&filesys_lock);
	for (int i = 0; i < cnt_page; i++)
	{
		page = spt_find_page(&thread_current()->spt, addr + i * PGSIZE);
		if (pml4_is_dirty(thread_current()->pml4, page->va))
		{
			file_write_at(page->file.file, page->va, page->file.read_bytes, page->file.ofs);
		}
		page->frame->cnt_page -= 1;
		file_close(page->file.file);
		hash_delete(&thread_current()->spt.spt_hash, &page->page_elem);
		pml4_clear_page(thread_current()->pml4, page->va);
	}
	if (!is_lock_held)
		lock_release(&filesys_lock);
}
