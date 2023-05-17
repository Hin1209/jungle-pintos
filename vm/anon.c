/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static struct list swap_slot_list;
static struct lock swap_lock;
static bool anon_swap_in(struct page *page, void *kva);
static bool anon_swap_out(struct page *page);
static void anon_destroy(struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void vm_anon_init(void)
{
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1);
	list_init(&swap_slot_list);
	lock_init(&swap_lock);
	for (int i = 0; i < disk_size(swap_disk); i += SLOT_SIZE)
	{
		struct swap_slot *slot = malloc(sizeof(struct swap_slot));
		list_init(&slot->page_list);
		slot->start_sector = i;
		list_push_back(&swap_slot_list, &slot->slot_elem);
	}
}

/* Initialize the file mapping */
bool anon_initializer(struct page *page, enum vm_type type, void *kva)
{
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
	page->pml4 = thread_current()->pml4;
	anon_page->slot = NULL;
	list_push_back(&page->frame->page_list, &page->out_elem);
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in(struct page *page, void *kva)
{
	struct anon_page *anon_page = &page->anon;
	struct list *page_list = &anon_page->slot->page_list;
	struct swap_slot *slot = anon_page->slot;
	lock_acquire(&swap_lock);
	while (!list_empty(page_list))
	{
		struct page *in_page = list_entry(list_pop_front(page_list), struct page, out_elem);
		for (int i = 0; i < SLOT_SIZE; i++)
		{
			disk_read(swap_disk, in_page->anon.slot->start_sector + i, in_page->va + DISK_SECTOR_SIZE * i);
		}
		in_page->frame = page->frame;
		page->frame->cnt_page += 1;
		list_push_back(&page->frame->page_list, &in_page->out_elem);
	}
	list_push_back(&swap_slot_list, &slot->slot_elem);
	lock_release(&swap_lock);
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out(struct page *page)
{
	struct anon_page *anon_page = &page->anon;
	anon_page->slot = list_entry(list_pop_front(&swap_slot_list), struct swap_slot, slot_elem);
	lock_acquire(&swap_lock);
	while (!list_empty(&page->frame->page_list))
	{
		struct page *out_page = list_entry(list_pop_front(&page->frame->page_list), struct page, out_elem);
		list_push_back(&anon_page->slot, &out_page->out_elem);
		out_page->anon.slot = anon_page->slot;
		if (pml4_is_dirty(out_page->pml4, out_page->va))
		{
			for (int i = 0; i < SLOT_SIZE; i++)
			{
				disk_write(swap_disk, out_page->anon.slot->start_sector + i, out_page->va + DISK_SECTOR_SIZE * i);
			}
		}
		pml4_clear_page(out_page->pml4, out_page->va);
	}
	lock_release(&swap_lock);
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy(struct page *page)
{
	struct anon_page *anon_page = &page->anon;
	page->frame->cnt_page -= 1;
}
