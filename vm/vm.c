/* vm.c: Generic interface for virtual memory objects. */

#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/mmu.h"
#include "vm/vm.h"
#include "vm/inspect.h"

static bool
install_page(void *upage, void *kpage, bool writable)
{
	struct thread *t = thread_current();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page(t->pml4, upage) == NULL && pml4_set_page(t->pml4, upage, kpage, writable));
}

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

	struct supplemental_page_table *spt = &thread_current()->spt; //현재 실행 중인 스레드의 보조 페이지 테이블
	struct page *exist_page;
	upage = pg_round_down(upage);
	/* Check wheter the upage is already occupied or not. */
	bool (*page_initializer)(struct page *, enum vm_type, void *kva);
	switch (VM_TYPE(type))
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

		/* TODO: Insert the page into the spt. */
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
	struct page tmp;
	tmp.va = va;
	struct hash_elem *h = hash_find(&spt->spt_hash, &(tmp));
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
	vm_alloc_page_with_initializer(VM_ANON, pg_round_down(addr), 1, NULL, NULL);
	vm_claim_page(pg_round_down(addr));
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
	if (not_present) { //페이지가 없어서
		if(user_rsp -8 == addr || pg_round_down(user_rsp)== pg_round_down(addr)||(user_rsp<addr && addr<USER_STACK)){
			vm_stack_growth(addr);
			return true;
		}
		page = spt_find_page(spt, pg_round_down(addr));
		if (page == NULL)
			exit(-1);
		if (write == 1 && page->writable == 0)
			exit(-1);

	}
	else if (write){ //쓸 수 없는데 쓰려고 했다
		exit(-1);
	}
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

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page(struct page *page)
{
	struct frame *frame = vm_get_frame();
	struct thread *curr = thread_current();

	/* Set links */
	frame->page = page;
	page->frame = frame;
	frame->cnt_page = 1;

	switch (VM_TYPE(page->operations->type))
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
	return page_a->va < page_b->va; //두 페이지의 가상 주소(va)를 비교하여 작은 주소 순서로 정렬
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

/* Copy supplemental page table from src to dst */ //src: 부모  dst: 자식
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
								  struct supplemental_page_table *src UNUSED)
{
	struct hash_iterator i;
	struct page *newpage;
	void *aux;
	hash_first(&i, &src->spt_hash);
	while (hash_next(&i))
	{
		struct page *page = hash_entry(hash_cur(&i), struct page, page_elem);
		switch (VM_TYPE(page->operations->type)){
			case VM_UNINIT:
				aux = malloc(sizeof(struct load));
				memcpy(aux, page->uninit.aux, sizeof(struct load));
				vm_alloc_page_with_initializer(page->uninit.type, page->va, page->writable, page->uninit.init, aux);
				break;
			case VM_ANON:
				vm_alloc_page_with_initializer(page->operations->type, page->va, page->writable, NULL, NULL);
				vm_claim_page(page->va);
				newpage = spt_find_page(dst, page->va);
				memcpy(newpage->va, page->frame->kva, PGSIZE);
				break;
			case VM_FILE:
				newpage = malloc(sizeof(struct page));
				struct file_page *file = &newpage->file;
				newpage-> va = page->va;
				newpage-> writable = page->writable;
				newpage-> operations = page->operations;
				spt_insert_page(&thread_current()->spt,newpage);
				newpage->frame = page->frame;
				newpage->frame->cnt_page += 1;
				newpage->file.file = file_reopen(page->file.file);
				newpage->file.file_length = page->file.file_length;
				newpage->file.ofs = page->file.ofs;
				newpage->file.read_bytes = page->file.read_bytes;
				newpage->file.zero_bytes = page->file.zero_bytes;
				pml4_set_page(thread_current()->pml4, newpage->va, page->frame->kva, page->writable);
				break;
		}
	}
	return true;  
}

void clear_page_hash(struct hash_elem *h, void *aux)
{
	struct page *page = hash_entry(h, struct page, page_elem);
	vm_dealloc_page(page);
}

/* Free the resource hold by the supplemental page table */
// supplemental page table에 의해 유지되던 모든 자원들을 free합니다. 
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
	hash_clear(&spt->spt_hash, clear_page_hash);
}
