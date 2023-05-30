#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "proc.h"

void swap_out_page(pagetable_t pagetable);
void put_in_memory(uint64 virtual_address, pagetable_t pagetable);
/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[]; // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t)kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

  // virtio mmio disk interface
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

  // PLIC
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);

  return kpgtbl;
}

// Initialize the one kernel_pagetable
void kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void kvminithart()
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
  if (va >= MAXVA)
    panic("walk");

  for (int level = 2; level > 0; level--)
  {
    pte_t *pte = &pagetable[PX(level, va)];
    if (*pte & PTE_V)
    {
      pagetable = (pagetable_t)PTE2PA(*pte);
    }
    else
    {
      if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)
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

  if (va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    return 0;
  if ((*pte & PTE_V) == 0)
    return 0;
  if ((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if (mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if (size == 0)
    panic("mappages: size");

  a = PGROUNDDOWN(va);
  last = PGROUNDDOWN(va + size - 1);
  for (;;)
  {
    if ((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if (*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V;
    if (a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if ((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for (a = va; a < va + npages * PGSIZE; a += PGSIZE)
  {
    if ((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if ((*pte & PTE_V) == 0 && (*pte & PTE_PG) == 0) // the parenthesis might be worng
      panic("uvmunmap: not mapped");
    if (PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if (do_free && (*pte & PTE_V) == 1)
    {
      uint64 pa = PTE2PA(*pte);
      kfree((void *)pa);
    }
#ifndef NONE
  struct proc *p = myproc();
  struct page *page;
    // if va in memory, delete it
    if (pagetable == p->pagetable && proc_is_not_os(p) && (*pte & PTE_V) && do_free)
      for (page = p->pages_in_memory; page < &p->pages_in_memory[MAX_PSYC_PAGES]; page++)
        if (page->virtual_address == a)
        {
          nullify_page_fields(page);
          p->psyc_count--;
        }

    // if va in swap, delete it
    if (proc_is_not_os(p) && pagetable == p->pagetable && (*pte & PTE_PG))
      for (page = p->pages_in_swapfile; page < &p->pages_in_swapfile[MAX_SWAP_PAGES]; page++)
        if (page->virtual_address == a)
        {
          nullify_page_fields(page);
          p->swap_count--;
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
  pagetable = (pagetable_t)kalloc();
  if (pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void uvmfirst(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if (sz >= PGSIZE)
    panic("uvmfirst: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);
  memmove(mem, src, sz);
}

int calc_page_index(struct page* page,struct page* start_arr){
  return (int)(page - start_arr);
}

int find_min_time_page_index()
{
  struct page *page;
  struct proc *p = myproc();
  int min_index = 0;
  uint64 min_time = (uint64)~0;
  for (page = p->pages_in_memory; page < &p->pages_in_memory[MAX_PSYC_PAGES]; page++)
  {
    if (!page->in_use)
      return calc_page_index(page , p->pages_in_memory);
    if (page->time <= min_time)
    {
      min_index = calc_page_index(page , p->pages_in_memory);
      min_time = page->time;
    }
  }
  return min_index;
}

int count_time_ones(struct page *page)
{
  int ones = 0, i = 0;
  uint64 one = 0;
  for (i = 0; i < 64; i++)
  {
    one = 1 << i;
    ones = page->time & one ? ones + 1 : ones;
  }
  return ones;
}
int lapa()
{
  struct proc *p = myproc();
  struct page *page;
  int min_ones = 100, index = -1, ones = 0;
  for (page = p->pages_in_memory; page < &p->pages_in_memory[MAX_PSYC_PAGES]; page++)
  {
    if (!page->in_use)
      return calc_page_index(page , p->pages_in_memory);

    ones = count_time_ones(page);
    if (ones < min_ones || (min_ones == ones &&  page->time < p->pages_in_memory[index].time))
    {
      min_ones = ones;
      index = calc_page_index(page , p->pages_in_memory);
    }
  }
  return index;
}

int scfifo()
{
  struct proc *p = myproc();
  int min_index = 0;
  pte_t *pte;

  for (;;)
  {
    min_index = find_min_time_page_index();

    pte = walk(p->pagetable, p->pages_in_memory[min_index].virtual_address, 0);
    if (*pte & PTE_A)
    {
      p->time++;
      p->pages_in_memory[min_index].time = p->time; // so he will be last
      *pte &= ~PTE_A;
    }
    else
      return min_index;
  }
}

int nfua()
{
  return find_min_time_page_index();
}

// need to implement
int get_to_swap_index()
{
#ifdef SCFIFO
  return scfifo();
#endif
#ifdef NFUA
  return nfua();
#endif
#ifdef LAPA
  return lapa();
#endif
  return 0; // should not get here .
}


struct page* 
find_free_entry (struct page pages[], int size)
{
	for (struct page* entry = pages; entry < &pages[size] ; entry++)
	{
		if (!entry->in_use)
			return entry;
	}
	return 0;
}

struct page*
get_page_by_va(struct page pages[], int size, uint64 va)
{
	for (struct page* page = pages; page < &pages[size]; page++)
	{
		if (page->virtual_address == va) return page;
	}
	return 0;
}

void swap_in_page(uint64 va)
{
  struct proc *p = myproc();


	va = PGROUNDDOWN(va);
	//get page with virtual_address == va
	struct page* to_swap_from = get_page_by_va(p->pages_in_swapfile, MAX_SWAP_PAGES, va); 
	if (!to_swap_from)
		panic("swap_in_page: Coulnd't find page from swapfile");
  int offset_in_file = calc_page_index(to_swap_from, p->pages_in_swapfile) * PGSIZE;;
	
	// new page for the swapped in data
	char *new_page = kalloc();

  // move the page data to the temp page
  readFromSwapFile(p, new_page, offset_in_file, PGSIZE);

	// make page in swapfile available for swap_out
  p->swap_count--;
	to_swap_from->in_use = 0;

	// make sure psyc memory has space for swap in
	if(p->psyc_count == MAX_PSYC_PAGES)
		swap_out_page(p->pagetable);

	// fix pte
	pte_t *pte = walk(p->pagetable, va, 0);
	*pte = PA2PTE(new_page) | PTE_FLAGS(*pte);

	// complete the swap_in
	put_in_memory(va, p->pagetable);
}

void swap_out_page(pagetable_t pagetable)
{
  struct proc *p = myproc();
  struct page *to_swap;
  struct page *swap_page_entry;
  pte_t *pte;
  uint64 pa;
  int offset_in_file;

  if(p->swap_count == MAX_SWAP_PAGES)
    panic("Swap file is full.");

  to_swap = &p->pages_in_memory[get_to_swap_index()];

  // move the page data to the swap pages

  // first find a free entry in the swap array
	swap_page_entry = find_free_entry(p->pages_in_swapfile, MAX_SWAP_PAGES); // assumes one is always available
	if (!swap_page_entry)
		panic("swap_out_page: Couldn't find new page to swap to");


  swap_page_entry->virtual_address = to_swap->virtual_address;
  pte = walk(pagetable, swap_page_entry->virtual_address, 0); // find the pte of the page we swapping
  pa = PTE2PA(*pte);
  offset_in_file = calc_page_index(swap_page_entry, p->pages_in_swapfile) * PGSIZE;
  swap_page_entry->in_use = 1;
  writeToSwapFile(p, (char *)pa, offset_in_file, PGSIZE);

  p->psyc_count--;
  p->swap_count++;
  to_swap->in_use = 0;
  to_swap->virtual_address = 0;

  *pte &= ~PTE_V;
  *pte |= PTE_PG;
  kfree((void *)pa);
  sfence_vma();
}

int get_available_memory_entry_index(struct proc *p)
{
  struct page *page;
  int i = 0;

  for (page = p->pages_in_memory; page < &p->pages_in_memory[MAX_PSYC_PAGES]; page++, i++)
    if (page->in_use == 0)
      return i;

  return -1;
}

void put_in_memory(uint64 virtual_address, pagetable_t pagetable)
{
  struct proc *p = myproc();
  struct page *page;

#ifdef NONE // should do nothing.
  return;
#endif

  // if it is a os proc, do nothing.
  if (!proc_is_not_os(p))
    return;

  if (p->psyc_count + p->swap_count == MAX_TOTAL_PAGES)
    panic("put_in_memory: too many pages");

  if (p->psyc_count == MAX_PSYC_PAGES)
    swap_out_page(pagetable);

  p->psyc_count++;

  page = &p->pages_in_memory[get_available_memory_entry_index(p)];
#ifdef NFUA
  page->time = 0;
#endif
#ifdef LAPA
  page->time = (uint64)~0;
#endif
#ifdef SCFIFO
  page->time = ++p->time;
#endif

  pte_t *pte = walk(pagetable, page->virtual_address, 0);
  *pte &= ~PTE_PG;
  *pte |= PTE_V;
  page->in_use = 1;
  page->virtual_address = virtual_address;
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if (newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for (a = oldsz; a < newsz; a += PGSIZE)
  {
    mem = kalloc();
    if (mem == 0)
    {
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R | PTE_U | xperm) != 0)
    {
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    put_in_memory(a, pagetable);
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
  if (newsz >= oldsz)
    return oldsz;

  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz))
  {
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++)
  {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0)
    {
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    }
    else if (pte & PTE_V)
    {
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz)
{
  if (sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  pte_t *new_pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for (i = 0; i < sz; i += PGSIZE)
  {
    if ((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if (!((*pte & PTE_V) || (*pte & PTE_PG)))
      panic("uvmcopy: page not present");

    if ((*pte & PTE_V) && (*pte & PTE_PG))
      panic("uvmcopy: bug, pte both in psyc and swap");

    if (*pte & PTE_PG)
    {
      new_pte = walk(new, i, 1);
      *new_pte |= PTE_FLAGS(*pte);
      continue;
    }

		// else we found a pa;
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if ((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char *)pa, PGSIZE);
    if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0)
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
void uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while (len > 0)
  {
    va0 = PGROUNDDOWN(dstva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (dstva - va0);
    if (n > len)
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
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while (len > 0)
  {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if (n > len)
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
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while (got_null == 0 && max > 0)
  {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if (n > max)
      n = max;

    char *p = (char *)(pa0 + (srcva - va0));
    while (n > 0)
    {
      if (*p == '\0')
      {
        *dst = '\0';
        got_null = 1;
        break;
      }
      else
      {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if (got_null)
  {
    return 0;
  }
  else
  {
    return -1;
  }
}
