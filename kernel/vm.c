#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "proc.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern struct proc proc[];

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// Initialize the one kernel_pagetable
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void
kvminithart()
{
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = 2; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if(size == 0)
    panic("mappages: size");
  
  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

struct page*
get_available_page()
{
	struct proc *p = myproc();
	for (struct page *page = p->pages; page < &p->pages[MAX_TOTAL_PAGES] ; page++)
		if (page->state == AVAILABLE)
			return page;
	return 0;
}

struct page*
get_page_with(uint64 va)
{
	struct proc *p = myproc();
	for (struct page *page = p->pages; page < &p->pages[MAX_TOTAL_PAGES] ; page++)
		if (page->va == va)
			return page;
	return 0;
}

// this function is called from the scheduler.
void
update_time(struct proc *p){
  pte_t* pte;
  for(struct page *page = p->pages; page < &p->pages[MAX_TOTAL_PAGES]; page++){
    if(page->state != IN_MEMORY){
      continue;
    }

      pte = walk(p->pagetable, page->va, 0);

      page->time = (page->time >> 1);
      if(*pte & PTE_A){
        page->time |= 0x8000000000000000; // the compiler want allow(1 << 63) ; 
        (*pte) &= ~PTE_A; //turn off access 
      }
    }
}

struct page*
get_lowest_time_page(struct proc *p){
  uint64 min_time = ~0;
  struct page* min_page = 0;
  for(struct page *page = p->pages; page < &p->pages[MAX_TOTAL_PAGES]; page++){
    if(page->state != AVAILABLE && p->count_in_mem < MAX_PSYC_PAGES)
      return page;

    if(page->state == IN_MEMORY && page->time < min_time)
      min_page = page;
      }

  return min_page;
}

// return the page to swap by nfua algorithm
struct page*
get_page_by_nfua(struct proc *p){
  return get_lowest_time_page(p);
}

int
calc_num_of_ones(int num){
  int num_of_ones = 0;
  while(num){
    num_of_ones = num % 2 ? num_of_ones + 1 : num_of_ones;
    num /= 2;
  }
  return num_of_ones;
}
// return the page to swap by lapa algorithm
struct page*
get_page_by_lapa(struct proc *p){
  struct page* to_swap = 0;
  int min_ones = 65; // maximum possible is 64
  for (struct page* page = p->pages; page < &p->pages[MAX_TOTAL_PAGES]; p++){
    if(page->state != AVAILABLE && p->count_in_mem < MAX_PSYC_PAGES)
      return page;

    if(page->state != IN_MEMORY)
      continue;

    int page_ones = calc_num_of_ones(page->time);
    if(page_ones > min_ones || (page_ones == min_ones && page->time < to_swap->time)) {
      to_swap = page;
      min_ones = page_ones;
    }
  }
  return to_swap;
}

// return the page to swap by scfifo algorithm
struct page*
get_page_by_scfifo(struct proc *p){
  struct page* page;
  pte_t* pte;

  for(;;){
    page = get_lowest_time_page(p);
    if(page->state == AVAILABLE)
      return page;

    pte = walk(p->pagetable, page->va, 0);
    if(!(*pte & PTE_A))
      return page;

    // if I got here that means I need to put page in the end of the queue
    page->time = ++p->time_counter;
  }
}

struct page*
get_page_to_swap_for(struct proc *p)
{
	#ifdef NONE
  return 0; // defualt value
  #endif
  #ifdef NFUA
  return get_page_by_nfua(p)  ;
  #endif
  #ifdef LAPA
  return get_page_by_lapa(p) ;
  #endif
  #ifdef SCFIFO
  return get_page_by_scfifo(p) ;
  #endif
}

int
swap_out(void)
{
	struct proc *p = myproc();
	// find a page to swap out based on algorithm
	struct page *to_swap = get_page_to_swap_for(p);
	if (!to_swap)
		panic("swap_out: couldn't find a page to swap out");

  if(to_swap->state == AVAILABLE) // there is a free page
    return 0;

	// calculate its offset in swapfile
	uint offset = (to_swap - p->pages) * PGSIZE;

	// write to swapfile
	pte_t *pte = walk(p->pagetable, PGROUNDDOWN(to_swap->va), 0);

	uint64 pa = PTE2PA(*pte);
	if (writeToSwapFile(p, (char *)pa, offset, PGSIZE) == -1)
		return -1;

	// fix states and counters
	*pte &= ~PTE_V;
	*pte |= PTE_PG;
	to_swap->state = SWAPPED_OUT;
	p->count_in_mem--;
	p->count_in_swap++;

	// free physical memory and TLB
	kfree((void*) pa);
	sfence_vma();

	return 0;
}

void
init_swaped_in_page(struct page* page){
  page->state = IN_MEMORY; // I know that you assign it before, I dont want to touch your code to much, and it is should be here.
  #ifdef NONE
  return ;// nothing to do, I thought about putting pubic but I think it is overkill...
  #endif
  #ifdef NFUA
  page->time = 0;  
  #endif
  #ifdef LAPA
  page->time = (uint64)(~0);  
  #endif
  #ifdef SCFIFO
  struct proc* p = myproc();
  page->time = ++p->time_counter; // first update the p->time_counter than assign
  #endif
}

int
swap_in(uint64 va)
{
	struct proc *p = myproc();
	va = PGROUNDDOWN(va);
	// find the swapped out page corresponding to 'va'
	struct page *to_swap = get_page_with(va);
	if (!to_swap)
		panic("swap_in: couldn't find a page corresponding to given va");
	if (to_swap->state != SWAPPED_OUT)
		panic("swap_in: found page is not SWAPPED_OUT");

	// make sure process has enough space in physical memory
	if (p->count_in_mem == MAX_PSYC_PAGES && 
			swap_out() < 0)
			return -1;
	
	//allocate new page in physical memory 
	void *new_page = kalloc();

	// read the page's content into a new file
	uint offset = (to_swap - p->pages) * PGSIZE;
	if (readFromSwapFile(p, (char*)new_page, offset, PGSIZE) == -1)
	{
		kfree((void*) new_page);
		return -1;
	}

	// fix translation between va and new_page
	pte_t *pte = walk(p->pagetable, va, 0);
	*pte = PA2PTE(((uint64)new_page)) | PTE_FLAGS(*pte);

	// fix pte and page state and counters
	*pte &= ~PTE_PG;
	*pte |= PTE_V;
	to_swap->state = IN_MEMORY;
	p->count_in_mem++;
	p->count_in_swap--;
  init_swaped_in_page(to_swap);

	return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;
  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if((*pte & PTE_V) == 0 && (*pte & PTE_PG) == 0)
      panic("uvmunmap: not mapped");
    if(PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");

		
    if(do_free && (*pte & PTE_V)){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
		

		#ifndef NONE
  	struct page *page = get_page_with(a);
    struct proc * p = myproc();

    if ((do_free && is_user_proc(p) && pagetable == p->pagetable)){
      
    // if va in memory, delete it
    if (do_free && page->state == IN_MEMORY)
		{
    	nullify_page_fields(page);
    	p->count_in_mem--;
    } 
		else if (page->state == SWAPPED_OUT)
		{
    	nullify_page_fields(page);
    	p->count_in_swap--;
		}
    }
		#endif

    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void
uvmfirst(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("uvmfirst: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W|PTE_R|PTE_X|PTE_U);
  memmove(mem, src, sz);
}



//assumes physical space is available
void
put_in_memory(uint64 va)
{
	struct proc *p = myproc();

	// get available page
	struct page *page = get_available_page();
	if (!page)
		panic("put_in_memory: no available page found");

	// set pte flags and page details
	pte_t *pte = walk(p->pagetable, va, 0);
	*pte &= ~PTE_PG;
	*pte |= PTE_V;
	page->state = IN_MEMORY;
	page->va = va;
  init_swaped_in_page(page);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
	#ifndef NONE
	struct proc *p = myproc();
	#endif

  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){

		#ifndef NONE
		if (is_user_proc(p))
			if (!get_available_page() || // allocation request exceeds maximum amount
					(p->count_in_mem == MAX_PSYC_PAGES && swap_out() < 0))   // cannot prepare physical space
			{
      	uvmdealloc(pagetable, a, oldsz);
      	return 0;
			}
		#endif

    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }

		#ifndef NONE
		if (is_user_proc(p))
			put_in_memory(a);
		#endif

  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if((pte & PTE_V) && (pte & (PTE_R|PTE_W|PTE_X)) == 0){
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    } else if(pte & PTE_V){
      panic("freewalk: leaf");
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte, *new_pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if((*pte & PTE_V) == 0 && (*pte & PTE_PG) == 0)
      panic("uvmcopy: page not present");
		if ((*pte & PTE_V) && (*pte & PTE_PG))
      panic("uvmcopy: bug, pte both in psyc and swap");


		if (*pte & PTE_PG) // page is swapped out, needs allocation
		{
			if ((new_pte = walk(new, i, 1)) == 0)
				goto err;
			*new_pte |= PTE_FLAGS(*pte);
			continue;
		}

		// else we found a pa
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0)
		{
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}
