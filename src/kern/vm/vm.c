/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <thread.h>
#include <current.h>
#include <mips/tlb.h>
#include <cpu.h>
#include <addrspace.h>
#include <vm.h>
#include <synch.h>
#include <syscall.h>

/* under dumbvm, always have 48k of user stack */
#define DUMBVM_STACKPAGES    12

/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

static  int pagenum;
static  int freeppn;
/* temp value for timestamp */
static	time_t tempsec;
static   uint32_t tempns;

/* core map array pointer */
static struct page* pages;

// the global lock for the core map

// the global lock for tlb
//static struct spinlock tlb_lk = SPINLOCK_INITIALIZER;

// flag for vm_bootstrap
static int  vm_bootflag;


void
vm_bootstrap(void)
{
	/* some paddrs */
	paddr_t firstpaddr;
	paddr_t lastpaddr;
	paddr_t freepaddr;
	

	/* obtain ram size, we can not use ram_stealmem any more */
	ram_getsize(&firstpaddr, &lastpaddr);

   /* get page num*/
	pagenum = lastpaddr / PAGE_SIZE;

   /* set core map pointer to firstpaddr  */
   pages = (struct page *)PADDR_TO_KVADDR(firstpaddr);
   
	/* find free paddr and coresponding ppage number, it is right after allocate coremap array*/
	freepaddr = firstpaddr + pagenum * (sizeof(struct page));
   freeppn = (freepaddr + PAGE_SIZE -1) / PAGE_SIZE;
   
   // lock_acquire(coremap_lk);
	spinlock_acquire(&stealmem_lock);

	/* initialize the core map array */

	for(int i = 0; i < pagenum; i++)
	{
		if(i < freeppn)
		{
			pages[i].page_state = S_FIXED;
     	   pages[i].as =NULL;
		   pages[i].va = 0;
         pages[i].npages = 1;
	   	gettime(&tempsec, &tempns);
         pages[i].time_stamp = tempsec;
		}else{
			pages[i].page_state = S_FREE;
      	pages[i].as =NULL;
			pages[i].va = 0;
     	   pages[i].npages = 0;
     	   pages[i].time_stamp = 0;
		}

	}
	//lock_release(coremap_lk); 
	spinlock_release(&stealmem_lock);

	/* set vm_bootstrap flag*/
	vm_bootflag = 1;
}

static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;
   if (vm_bootflag != 1){
		spinlock_acquire(&stealmem_lock);
		addr = ram_stealmem(npages);
		spinlock_release(&stealmem_lock);
	}else{
   	 addr = page_nallco(npages);
	}
	return addr;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t 
alloc_kpages(int npages)
{
	paddr_t pa;
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void 
free_kpages(vaddr_t addr)
{  
	// kernel addresss mapped memery free, get physical memery number
	int ppn = (addr - MIPS_KSEG0)/PAGE_SIZE;

	if ((vm_bootflag == 1) && (ppn >= freeppn)){

		//acquire lock for modifying coremap
		spinlock_acquire(&stealmem_lock);

		// release contiguous memory space
		if((pages[ppn-1].npages <= pages[ppn].npages) && (pages[ppn].page_state != S_FREE)){

   		for(int i = pages[ppn].npages; i>= 1; i--,ppn--){
				pages[ppn].page_state = S_FREE;
			}
		}
		//release the lock
		spinlock_release(&stealmem_lock);
	}
}

/*  allocate/free one physical page */
paddr_t
page_allo(struct addrspace* as, vaddr_t va)
{
	int oldest = 0;
	//int spl;
   // to check if there is page dir_two 
	vaddr_t* dir_one = (vaddr_t *)as->page_table_addr;
	uint32_t  index_one = ((va >> 22) & PAGE_DIRECTORY); 

	if(dir_one[index_one] == 0){
		dir_one[index_one] = alloc_kpages(1);
		if (dir_one[index_one] == 0){
			return 0;
		}
	}

	// try to find a free physical page, if not found, choose the oldest one
	gettime(&tempsec, &tempns);
	for(int i = freeppn; i < pagenum; i++)
	{
		spinlock_acquire(&stealmem_lock);
		// record the oldest page
		if(pages[i].time_stamp < tempsec){
			oldest = i;
		}
		// find a free page
		if(pages[i].page_state == S_FREE){

			// update coremap
			pages[i].page_state = S_DIRTY;
			pages[i].as = as;
			pages[i].va = va;
			pages[i].npages = 1;
			gettime(&tempsec, &tempns);
			pages[i].time_stamp = tempsec;

         bzero((void *)(PADDR_TO_KVADDR(i*PAGE_SIZE)),PAGE_SIZE);
         

			//update page table , set permission as readable and wriable
			vaddr_t* dir_two = (vaddr_t *)(dir_one[index_one]);
			uint32_t index_two = ((va >> 12) & PAGE_DIRECTORY);
			if(dir_two[index_two] == 0){
				dir_two[index_two] = (((paddr_t)i*PAGE_SIZE) & PAGE_NUMBER)
				                       	|PAGE_EXIST | PAGE_READ |PAGE_WRITE;
			}else{
				// if permission already set, keep it 
				(dir_two[index_two]) |=((((paddr_t)i*PAGE_SIZE) & PAGE_NUMBER) | PAGE_EXIST);
			}
			//update tlb
		//	spl = splhigh();
		//	spinlock_acquire(&tlb_lk);

		//	uint32_t ehi;
		//	uint32_t elo;
		//	paddr_t paddr = (paddr_t)i*PAGE_SIZE;
		//	KASSERT((paddr & PAGE_NUMBER) == paddr);
		//	ehi = va & PAGE_NUMBER;
		//	elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
        // tlb_random(ehi,elo);

		//	spinlock_release(&tlb_lk);
         //splx(spl);

			spinlock_release(&stealmem_lock);
			return (paddr_t) i*PAGE_SIZE;
		}
		// if we do swapiing thing, add code here to replace the oldest page
     spinlock_release(&stealmem_lock);
	}
		spinlock_release(&stealmem_lock);
		return 0;
}

void
page_free(struct addrspace* as, vaddr_t va)
{
	int spl;
	// look up page table, get pointer to physical address
	vaddr_t* page_entry = page_walk(as,va,0);
	KASSERT(page_entry != NULL);

	uint32_t index_two = ((va >> 12) & PAGE_DIRECTORY);
	paddr_t paddr = page_entry[index_two] & PAGE_NUMBER;
   KASSERT(paddr != 0);
	// unmap in page table
	page_entry[index_two] = 0;

	//change coremap
	spinlock_acquire(&stealmem_lock);
   int index = paddr / PAGE_SIZE;
	pages[index].page_state = S_FREE;
	pages[index].va = 0;
	pages[index].as = NULL;
	pages[index].time_stamp = 0;
	pages[index].npages = 0;
	spinlock_release(&stealmem_lock);
	
	//shut down tlb
	uint32_t ehi;
	ehi = va & PAGE_NUMBER;
	spl = splhigh();

	int i = tlb_probe(ehi, 0);
	if(i >= 0){
	  tlb_write(TLBHI_INVALID(i),TLBLO_INVALID(),i);
	}
	splx(spl);
}

/*  allocate n contiguous pages after vm bootstrapt */
paddr_t
page_nallco(int npages)
{  
	paddr_t paddr = 0;
	paddr_t temppaddr = (paddr_t) (freeppn * PAGE_SIZE);
	int count = 0;
	int freebegin = freeppn;


	/* first ttry to find n contiguous free pages*/
	spinlock_acquire(&stealmem_lock);
   for(int i = freeppn; i < pagenum ; i++)
	{
	  if(pages[i].page_state == S_FREE){
	  	  count ++;
		  if(count == npages){
        	paddr = temppaddr;
			int decount = count;
			for(; freebegin <= i; freebegin ++){
				pages[freebegin].va = PADDR_TO_KVADDR((paddr_t)(freebegin * PAGE_SIZE));
				pages[freebegin].page_state = S_DIRTY;
				pages[freebegin].npages = decount;
				decount --;
				gettime(&tempsec, &tempns);
				pages[freebegin].time_stamp = tempsec;
				if(decount == 0){
			      spinlock_release(&stealmem_lock);
			      bzero((void *)(PADDR_TO_KVADDR(paddr)), npages*PAGE_SIZE);
		      	return paddr;
				}
			}
		  }
	  }else{
		  count = 0;
        temppaddr = (paddr_t)((i+1) * PAGE_SIZE);
		  freebegin = i+1;
	  }
	}
	// if no such n contigous pages, do swapping	
   spinlock_release(&stealmem_lock);
	return paddr;
}

void
vm_tlbshootdown_all(void)
{
	int spl;
	spl = splhigh();
   // shut down all tlb entries
	
	for (int i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
//	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	vaddr_t vaddr = ts->ts_vaddr;
	vaddr &= PAGE_NUMBER;
	int spl;
	spl = splhigh();
	int i = tlb_probe(vaddr, 0);
	if(i >= 0){
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
//	panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	struct addrspace *as;
	vaddr_t * page_entry;
	paddr_t paddr;
	uint32_t ehi, elo;
	int spl;
	uint32_t index_two;

	as = curthread->t_addrspace;
	if (as == NULL) {
		return EFAULT;
	}
   
	//Assert that the address space has been set up properly. a
	KASSERT(as->heap_start != 0);
	KASSERT(as->heap_end !=0);
	KASSERT((as->heap_start & PAGE_FRAME) == as->heap_start);
   KASSERT(as->page_table_addr != 0);
	 
	faultaddress &= PAGE_FRAME;

	switch (faulttype) {
	    case VM_FAULT_READONLY:
	         page_entry = page_walk(as, faultaddress,0);
				if(page_entry == NULL){
					return ENOMEM;
				}

				index_two = ((faultaddress	>> 12) & PAGE_DIRECTORY);
				paddr = page_entry[index_two] & PAGE_NUMBER;
				KASSERT(paddr != 0);
				//int ppn = paddr / PAGE_SIZE;
            // check permission bits
				if((page_entry[index_two] & PAGE_WRITE) != 0){

					ehi = faultaddress & PAGE_NUMBER;
					elo = paddr | TLBLO_DIRTY | TLBLO_VALID;

					spl = splhigh();

					//spinlock_acquire(&tlb_lk);
					int i = tlb_probe(ehi, 0);
					tlb_write(ehi,elo,i);
					//spinlock_release(&tlb_lk);

					splx(spl);

					// update coremap, now the page is dirty
				//	spinlock_acquire(&stealmem_lock);
				//	pages[ppn].page_state = S_DIRTY;
				//	spinlock_release(&stealmem_lock);
					return 0;

				}else{
				     panic("I can not handle this, user is trying to write into read only page \n");
				}
			 // panic("got read only error");
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
				// for case tlb miss on load/store
				page_entry = page_walk(as, faultaddress,1);
				if(page_entry == NULL){
					return ENOMEM;
				}

				index_two = ((faultaddress	>> 12) & PAGE_DIRECTORY);
				paddr = page_entry[index_two] & PAGE_NUMBER;
				KASSERT(paddr != 0);

				//  Disable interrupts on this CPU while frobbing the TLB. 
				spl = splhigh();

				ehi = faultaddress & PAGE_NUMBER;
			   elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
            if(tlb_probe(ehi,0) >= 0){
					splx(spl);
					return 0;
				}
			   tlb_random(ehi,elo);
				splx(spl);
				return 0;
				break;
	    default:
				return EINVAL;
	}
}

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}
 
	//  Initialize as needed.
	as->heap_start = 0;
	as->heap_end = 0;
	as->page_table_addr = alloc_kpages (1);
	if((void *)as->page_table_addr == NULL){
		return NULL;
	}

	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	// create new second level page table and copy present pages
	vaddr_t* old_dir_one = (vaddr_t *)old->page_table_addr;
	vaddr_t* new_dir_one = (vaddr_t *)newas->page_table_addr;
	for(uint32_t index_one = 0; index_one < 1024; index_one++){
		//if there is second level page table,then to find present pages
		if(old_dir_one[index_one] != 0){
			new_dir_one[index_one] = alloc_kpages(1);
			if(new_dir_one[index_one] == 0){
				return ENOMEM;
			}
			vaddr_t* old_dir_two = (vaddr_t *)old_dir_one[index_one];
		   vaddr_t* new_dir_two = (vaddr_t *)new_dir_one[index_one];
			// travese the page table
			for(uint32_t index_two = 0; index_two < 1024; index_two++){
				// if page is present
				if(((old_dir_two[index_two] & PAGE_EXIST) != 0) &&
					  	((old_dir_two[index_two] & PAGE_NUMBER) != 0 )){
					// alloc a new page for new as
							vaddr_t va = ((index_one << 22) & PAGE_DIR_VA_HL) + 
						          ((index_two << 12) & PAGE_DIR_VA_M); 
					paddr_t paddr = page_allo(newas,va);
					if(paddr == 0){
						return ENOMEM;
					}
					//update new page table and copy permissiom bits
					new_dir_two[index_two] = (paddr & PAGE_NUMBER) |
					  							 	(old_dir_two[index_two] & (PAGE_PERMIT | PAGE_EXIST));
					KASSERT((paddr & PAGE_NUMBER) == paddr);
					memmove((void *)PADDR_TO_KVADDR(paddr),
								(const void *)PADDR_TO_KVADDR(old_dir_two[index_two] & PAGE_NUMBER),PAGE_SIZE);
				}	
				// if page in on disk
			}
		}
	}

	// copy heap information
	newas->heap_start = old->heap_start;
	newas->heap_end = old->heap_end;

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	
	// free pages and page table
	vaddr_t* dir_one = (vaddr_t *)as->page_table_addr;

	// traverse the first level of page table
	for(uint32_t  index_one = 0; index_one < 1024; index_one++){
	   // if second level of page table is not zero, try to find pages
		if(dir_one[index_one] != 0){
 		  vaddr_t* dir_two = (vaddr_t *)dir_one[index_one];
		  // traverse the second level of page table
		  for(uint32_t  index_two = 0; index_two < 1024; index_two++){
			  // if there is mapping, then free the page
				if((dir_two[index_two] & PAGE_NUMBER) != 0){
					vaddr_t t1 = (index_one << 22);
					t1 &= PAGE_DIR_VA_HL;
					vaddr_t t2 = index_two << 12;
					t2 &= PAGE_DIR_VA_M;
					vaddr_t va =t1 + t2;
					// unmap in page table
					paddr_t paddr = dir_two[index_two] & PAGE_NUMBER;
					dir_two[index_two] = 0;

					//change coremap
					spinlock_acquire(&stealmem_lock);
					int index = paddr / PAGE_SIZE;
					pages[index].page_state = S_FREE;
					pages[index].va = 0;
					pages[index].as = NULL;
					pages[index].time_stamp = 0;
					pages[index].npages = 0;
					spinlock_release(&stealmem_lock);
					
					//shut down tlb
					uint32_t ehi;
					ehi = va & PAGE_NUMBER;
					int spl = splhigh();

					int i = tlb_probe(ehi, 0);
					if(i >= 0){
					  tlb_write(TLBHI_INVALID(i),TLBLO_INVALID(),i);
					}
					splx(spl);
				}
		  }
		  // free second level page table itself
		  free_kpages(dir_one[index_one]);
		  dir_one[index_one] = 0;
		}
	}
	// free the first level of page table
	free_kpages(as->page_table_addr);
	as->page_table_addr = 0;
	// free as itself
	kfree(as);
}

void
as_activate(struct addrspace *as)
{
	
	int i, spl;

	(void)as;

	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);

}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages; 
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;
	npages = sz / PAGE_SIZE;

   // count page number
	for(size_t count = 0; count < npages; count ++){
		vaddr_t* page_entry = page_walk(as, vaddr,0);
		if(page_entry == NULL){
			return ENOMEM;
		}
		uint32_t index_two = ((vaddr >> 12) & PAGE_DIRECTORY);
	   page_entry[index_two] = (~PAGE_PERMIT) & (readable | writeable | executable);
		vaddr += (vaddr_t)PAGE_SIZE;
		if(count == npages -1){
		  KASSERT(((vaddr + (vaddr_t)PAGE_SIZE) & PAGE_NUMBER)  == (vaddr + (vaddr_t)PAGE_SIZE));
        as->heap_start = vaddr;
		  as->heap_end = vaddr;
		}
	}
	return 0;
}

int
as_prepare_load(struct addrspace *as)
{
	 
	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	
	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	(void)as;

	// Initial user-level stack pointer 	
	*stackptr = USERSTACK;
	
	return 0;
}


vaddr_t*
page_walk(struct addrspace* as, vaddr_t va, int flag)
{
	//if no entry, alloc a page, if has one, just return
	KASSERT(as->page_table_addr != 0);
   
	vaddr_t* dir_one = (vaddr_t*)as->page_table_addr;
   uint32_t index_one = ((va >> 22) & PAGE_DIRECTORY); 

	// no page dir_two
	if(dir_one[index_one] == 0){
		dir_one[index_one] = alloc_kpages(1);
		if(dir_one[index_one] == 0){
			return NULL;
		}
	}
	vaddr_t* dir_two = (vaddr_t *)(dir_one[index_one]);
	uint32_t index_two = ((va >> 12) & PAGE_DIRECTORY);
	// no mapping
	if(dir_two[index_two] == 0 ||
		  	(((dir_two[index_two] & PAGE_NUMBER) == 0) && ((dir_two[index_two] & PAGE_PERMIT )!= 0))){
         if(flag == 0){
				return (vaddr_t*) dir_one[index_one];
			}
		   paddr_t paddr =  page_allo(as,va);
			if(paddr == 0){
				return NULL;
			}
			return (vaddr_t*) dir_one[index_one];
	}else {
		return (vaddr_t*) dir_one[index_one];
	}
}
