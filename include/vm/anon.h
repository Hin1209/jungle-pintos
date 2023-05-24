#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
#include "devices/disk.h"

#define SLOT_SIZE 8

struct page;
enum vm_type;

struct anon_page
{
    void *aux;
    // 디스크로 쫓겨난 위치 정보
};

struct swap_slot
{
    disk_sector_t start_sector;
    struct list page_list;
    struct list_elem slot_elem;
};

void vm_anon_init(void);
bool anon_initializer(struct page *page, enum vm_type type, void *kva);

#endif
