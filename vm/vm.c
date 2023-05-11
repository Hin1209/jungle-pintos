/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_table);

}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;
	upage = pg_round_down(upage);
	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		struct page *newpage = calloc(1, sizeof(struct page));
		bool (*page_initializer)(struct page*, enum vm_type, void *kva);
		switch(type)
		{
			case VM_ANON:
			page_initializer = anon_initializer;
			break;
			case VM_FILE:
			page_initializer = file_backed_initializer;
			break;
		}
		newpage->writable = writable;
		uninit_new(newpage, upage, init, type, aux, page_initializer);

		spt_insert_page(spt, newpage);
		return true;
		/* TODO: Insert the page into the spt. */
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function. */
	struct page tmp;
	tmp.va = va;
	struct hash_elem *h = hash_find(&spt->spt_hash, &(tmp.page_elem));
	if(h==NULL)
	{
			return NULL;
	}
	page = hash_entry(h, struct page, page_elem); 
	return page;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED, struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */
	if (hash_insert(&spt->spt_hash, &page->page_elem))
	{
		succ = true;
	}
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;

  frame = calloc(1, sizeof(frame));
	void *upage = palloc_get_page(PAL_USER | PAL_ZERO);
	if(upage == NULL) // null이면, panic
	{
		PANIC("not implemented!");
	}
	else
	{
		frame->page = NULL;
		frame->kva = upage;
		list_push_back(&frame_table, &frame->fram_elem);
	}
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
  // 유효한 페이지 오류인지 확인
	page = spt_find_page(spt, addr);
	if(page == 	NULL)
	{
		exit(-1);
	}
	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct thread *curr = thread_current();
	struct page *page = NULL;
	/* TODO: Fill this function */
	struct hash_elem *h = spt_find_page(&curr->spt, va);// 수정 필요 hash_find(&curr->spt, va);
	page = hash_entry(h, struct page, page_elem);
	if (page == NULL)
	{
		// 훗날의 내가
	}
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct thread *curr = thread_current();
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	// if anon, file은 디스크에서 꺼내와야하기 때문에 나중에
	switch(page->operations->type)
	{
		case VM_UNINIT:
			pml4_set_page(curr->pml4, page->va, frame->kva, page->writable);
			break;
		case VM_FILE:
			break;
		case VM_ANON:
			break;
	}
	return swap_in (page, frame->kva);
}

bool page_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux){
	struct page *page_a = hash_entry(a, const struct page, page_elem);
	struct page *page_b = hash_entry(b, const struct page, page_elem);
	return page_a->va < page_b->va;
}

unsigned int hash_va(const struct hash_elem *p, void *aux UNUSED)
{
	struct page *page = hash_entry(p, struct page, page_elem);
	return hash_int(page->va);
}
/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->spt_hash, hash_va, page_less_func, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}
