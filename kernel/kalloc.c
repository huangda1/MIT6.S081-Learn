// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU];

void
kinit()
{
  for (int i = 0; i < NCPU; ++i) {
    initlock(&kmem[i].lock, "kmem");
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  push_off();
  int id = cpuid();
  acquire(&kmem[id].lock);
  r->next = kmem[id].freelist;
  kmem[id].freelist = r;
  release(&kmem[id].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  push_off();
  int id = cpuid();
  

  acquire(&kmem[id].lock);
  r = kmem[id].freelist;
  if(r) {
    kmem[id].freelist = r->next;
  } else {
    // steal one page
    // for (int i = 0; i < NCPU; i++) {
    //   if (i == id) continue;
    //   acquire(&kmem[i].lock);
    //   if (kmem[i].freelist) {
    //     r = kmem[i].freelist;
    //     kmem[i].freelist = r->next;
    //   }
    //   release(&kmem[i].lock);
    //   if(r) 
    //     break;
    // }

    // steal many pages
    // int steal_pages = 64;
    // for (int i = 0; i < NCPU; i++) {
    //   if (i == id) continue;
    //   acquire(&kmem[i].lock);
    //   r = kmem[i].freelist;
    //   while (r && --steal_pages) {
    //     kmem[i].freelist = r->next;
    //     r->next = kmem[id].freelist;
    //     kmem[id].freelist = r;
    //     r = kmem[i].freelist;
    //   }
    //   release(&kmem[i].lock);
    //   if (!steal_pages) break;
    // }
    // r = kmem[id].freelist;
    // if (r)
    //   kmem[id].freelist = r->next;
  // }
  
  // fatal error : 以上两种写法会发生死锁，两个线程互相要偷对方的页，持有自己的锁去获取别人的锁
  release(&kmem[id].lock);
  struct run *steal_freelist = 0;
  int steal_pages = 64;
    for (int i = 0; i < NCPU; i++) {
      if (i == id) continue;
      acquire(&kmem[i].lock);
      r = kmem[i].freelist;
      while (r && --steal_pages) {
        kmem[i].freelist = r->next;
        r->next = steal_freelist;
        steal_freelist = r;
        r = kmem[i].freelist;
      }
      release(&kmem[i].lock);
      if (!steal_pages) break;
    }
    acquire(&kmem[id].lock);
    kmem[id].freelist = steal_freelist;
    r = kmem[id].freelist;
    if (r)
      kmem[id].freelist = r->next;
  }
  // 先释放自己的锁，然后再去获取别人的锁，同时使用steal_freelist使得别的线程依然看到自己的freelist为空，不会更改自己的freelist
  // 同时此时关闭了中断，不会有别的线程重复偷页
  
  release(&kmem[id].lock);
  pop_off();

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk

  return (void*)r;
}
