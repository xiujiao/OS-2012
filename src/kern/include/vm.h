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

#ifndef _VM_H_
#define _VM_H_

/*
 * VM system-related definitions.
 *
 * You'll probably want to add stuff here.
 */


#include <machine/vm.h>
#include <clock.h>

struct lock;
struct addrspace;

/* Fault-type arguments to vm_fault() */
#define VM_FAULT_READ        0    /* A read was attempted */
#define VM_FAULT_WRITE       1    /* A write was attempted */
#define VM_FAULT_READONLY    2    /* A write to a readonly page was attempted*/

// permission for a page
#define PAGE_READ     0x4  /* a page is readable */
#define PAGE_WRITE    0x2  /* a page is writable*/
#define PAGE_EXEC     0x1  /* a page is excutable */
#define PAGE_PERMIT   0x7 /* a page permission is set*/

// mask for physical page number, page entry exit bit,disk bit
#define PAGE_NUMBER  0xfffff000  
#define PAGE_EXIST   0x00000800
#define PAGE_DISK    0x00000400

// mask for getting page directery from vaddr
#define PAGE_DIRECTORY 0x000003ff

//mask for getting vaddr form directery
#define PAGE_DIR_VA_HL 0xcff00000
#define PAGE_DIR_VA_M  0x003ff000

// page state
typedef enum {
	S_FIXED, /* can not be swapped out*/
	S_FREE, /* valid*/
	S_CLEAN, /* is already used, but clean*/
	S_DIRTY, /* dirty,has been writeen*/
} pagestate_t;


// struct for a page
struct page {
	// where the page is mapped to
	struct addrspace* as;
	vaddr_t va;

	// page state
	pagestate_t page_state;

	//number of continguous pages allocation
   int npages;

	// time information of the page, used for FIFO page replacement
	time_t time_stamp;
};


/* Initialization function */
void vm_bootstrap(void);

/* Fault handling function called by trap code */
int vm_fault(int faulttype, vaddr_t faultaddress);

/* Allocate/free kernel heap pages (called by kmalloc/kfree) */
vaddr_t alloc_kpages(int npages);
void free_kpages(vaddr_t addr);

/* TLB shootdown handling called from interprocessor_interrupt */
void vm_tlbshootdown_all(void);
void vm_tlbshootdown(const struct tlbshootdown *);

/* allocate/ free one page to update coremap */
paddr_t  page_allo(struct addrspace* as, vaddr_t va);
void page_free(struct addrspace* as, vaddr_t va);

/* allocate n contimugous pages after vm_bootstrap*/
paddr_t page_nallco(int npages);

#endif /* _VM_H_ */
