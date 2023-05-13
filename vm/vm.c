/* vm.c: Generic interface for virtual memory objects. */

#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "vm/vm.h"
#include "vm/inspect.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void)
{
	vm_anon_init();
	vm_file_init();
#ifdef EFILESYS /* For project 4 */
	pagecache_init();
#endif
	register_inspect_intr();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_table);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type(struct page *page)
{
	int ty = VM_TYPE(page->operations->type);
	switch (ty)
	{
	case VM_UNINIT:
		return VM_TYPE(page->uninit.type);
	default:
		return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
									vm_initializer *init, void *aux)
{

	ASSERT(VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current()->spt;
	struct page *exist_page;
	upage = pg_round_down(upage);
	/* Check wheter the upage is already occupied or not. */
	bool (*page_initializer)(struct page *, enum vm_type, void *kva);
	switch (type)
	{
	case VM_ANON:
		page_initializer = anon_initializer;
		break;
	case VM_FILE:
		page_initializer = file_backed_initializer;
		break;
	}
	if ((exist_page = spt_find_page(spt, upage)) == NULL)
	{
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *newpage = calloc(1, sizeof(struct page));
		uninit_new(newpage, upage, init, type, aux, page_initializer);
		newpage->writable = writable;
		if (aux != NULL)
		{
			memcpy(&(newpage->running_file), aux, sizeof(struct file *));
			memcpy(&(newpage->ofs), (aux + 8), sizeof(int));
			memcpy(&(newpage->read_bytes), (aux + 12), sizeof(int));
		}

		/* TODO: Insert the page into the spt. */
		spt_insert_page(spt, newpage);
		return true;
	}
	else
	{
		vm_dealloc_page(exist_page);
		struct page *newpage = calloc(1, sizeof(struct page));
		uninit_new(newpage, upage, init, type, aux, page_initializer);
		newpage->writable = writable;
		spt_insert_page(spt, newpage);
		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED)
{
	struct page *page = NULL;
	/* TODO: Fill this function. */
	struct page tmp;
	tmp.va = va;
	struct hash_elem *h = hash_find(&spt->spt_hash, &(tmp.page_elem));
	if (h == NULL)
	{
		return NULL;
	}
	page = hash_entry(h, struct page, page_elem);

	return page;
}

/* Insert PAGE into spt with validation. */
bool spt_insert_page(struct supplemental_page_table *spt UNUSED,
					 struct page *page UNUSED)
{
	int succ = false;
	if (hash_insert(&spt->spt_hash, &page->page_elem) == NULL)
		succ = true;
	return succ;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	vm_dealloc_page(page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim(void)
{
	struct frame *victim = NULL;
	/* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame(void)
{
	struct frame *victim UNUSED = vm_get_victim();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame(void)
{
	struct frame *frame = NULL;
	frame = calloc(1, sizeof(struct frame));
	void *upage = palloc_get_page(PAL_USER | PAL_ZERO);
	if (upage == NULL)
		PANIC("not implemented!"); // evict 구현
	else
	{
		frame->page = NULL;
		frame->kva = upage;
		list_push_back(&frame_table, &frame->frame_elem);
	}

	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth(void *addr UNUSED)
{
	struct page *newpage = calloc(1, sizeof(struct page));
	uninit_new(newpage, pg_round_down(addr), NULL, VM_ANON, NULL, anon_initializer);
	spt_insert_page(&thread_current()->spt, newpage);
	vm_do_claim_page(newpage);
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED)
{
}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
						 bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
{
	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
	struct page *page = NULL;
	uint64_t user_rsp = thread_current()->user_rsp;
	if (user_rsp - 8 == addr || pg_round_down(user_rsp) == pg_round_down(addr) || (user_rsp < addr && addr < USER_STACK))
	{
		vm_stack_growth(addr);
		return true;
	}

	page = spt_find_page(spt, pg_round_down(addr));
	if (page == NULL)
		exit(-1);

	return vm_do_claim_page(page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void vm_dealloc_page(struct page *page)
{
	destroy(page);
	free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED)
{
	struct thread *curr = thread_current();
	struct page *page = spt_find_page(&curr->spt, va);
	if (page == NULL)
	{
	}

	return vm_do_claim_page(page);
}

static bool
install_page(void *upage, void *kpage, bool writable)
{
	struct thread *t = thread_current();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page(t->pml4, upage) == NULL && pml4_set_page(t->pml4, upage, kpage, writable));
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page(struct page *page)
{
	struct frame *frame = vm_get_frame();
	struct thread *curr = thread_current();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	switch (page->operations->type)
	{
	case VM_UNINIT:
		if (!install_page(page->va, frame->kva, page->writable))
			PANIC("FAIL");
		break;
	case VM_ANON:
		if (!install_page(page->va, frame->kva, page->writable))
			PANIC("FAIL");
		break;
	case VM_FILE:
		break;
	}

	return swap_in(page, frame->kva);
}

/* Initialize new supplemental page table */
bool hash_page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
	struct page *page_a = hash_entry(a, struct page, page_elem);
	struct page *page_b = hash_entry(b, struct page, page_elem);
	return page_a->va < page_b->va;
}

unsigned int hash_va(const struct hash_elem *p, void *aux UNUSED)
{
	struct page *page = hash_entry(p, struct page, page_elem);
	return hash_bytes(&page->va, sizeof(page->va));
}

void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED)
{
	hash_init(&spt->spt_hash, hash_va, hash_page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
								  struct supplemental_page_table *src UNUSED)
{
	struct hash_iterator i;
	hash_first(&i, &src->spt_hash);
	while (hash_next(&i))
	{
		struct page *page = hash_entry(hash_cur(&i), struct page, page_elem);
		struct page *newpage = malloc(sizeof(struct page));
		memcpy(newpage, page, sizeof(struct page));
		if (page->operations->type != VM_UNINIT)
		{
			vm_do_claim_page(newpage);
			memcpy(newpage->va, page->frame->kva, PGSIZE);
		}
		spt_insert_page(dst, newpage);
	}
	return true;
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	struct hash_iterator i;
	struct page *remove_page = NULL;
	hash_first(&i, &spt->spt_hash);
	while (hash_next(&i))
	{
		if (remove_page != NULL)
			vm_dealloc_page(remove_page);
		remove_page = hash_entry(hash_cur(&i), struct page, page_elem);
	}
}
