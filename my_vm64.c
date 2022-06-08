#include "my_vm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <pthread.h>

/*
Function responsible for allocating and setting your physical memory 
*/
char *heap;
unsigned long long numPhysPages; //number of pages in (1GB) physical memory
unsigned long long int physBitMapChars; //number of chars needed for physical memory bitmap (1 bit per page)
char *pbitmap; //physcial bitmap
unsigned long long numVirtPages; //number of pages in (4GB) virt addr space
unsigned long long int virtBitMapChars; //number of chars needed for virt addr bitmap (1 bit per page)
char *vbitmap; //virtual bitmap
unsigned long long ****PD; //Outer-level page directory for the entire Page Table
int numOffsetBits; //number of bits for the page offset - offset into the page
int numPD1bits; //number of bits for indexing PD
int numPD2bits;
int numPD3bits;
int numPTbits; //number of bits for indexing inner-level PT
int maxNumPDES; //maximum number of PDEs
int maxNumPTEs; //maximum number of PTEs per page
int initialize = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; //thread safe locks
int tlbChecks = 0;
int tlbMisses = 0;

//tlb[TLB_ENTRIES];
//will be used when we allocate pages and need to set bitmap indices to 1
//will be used when we free pages and need to set bitmap indices to 0
void set_bit_at_index(char *bitmap, unsigned long long int index)
{
	unsigned long long int block = index/8;
        int offset = index%8;
        int count = 1;
        for(int i = 0; i < offset; i++) {
                count*=2;
        }
        bitmap[block] = bitmap[block] | (int)(128/count);

    return;
}
void free_bit_at_index(char *bitmap, unsigned long long int index) {
	unsigned long long int block = index/8;
	int offset = index%8;
	if(offset == 0)
		bitmap[block] = bitmap[block] & 127;
	if(offset == 1)
                bitmap[block] = bitmap[block] & 191;
	if(offset == 2)
                bitmap[block] = bitmap[block] & 223;
	if(offset == 3)
                bitmap[block] = bitmap[block] & 239;
	if(offset == 4)
                bitmap[block] = bitmap[block] & 247;
	if(offset == 5)
                bitmap[block] = bitmap[block] & 251;
   	if(offset == 6)
                bitmap[block] = bitmap[block] & 253;
	if(offset == 7)
                bitmap[block] = bitmap[block] & 254;
	return;
}
/*
 * Function 3: GETTING A BIT AT AN INDEX
 * Function to get a bit at "index"
 * Will be used to find a free page in bitmaps
 */
int get_bit_at_index(char *bitmap, unsigned long long int index)
{
    unsigned long long int block = index/8;
    int offset = index%8;
    int count = 1;
    for(int i = 0; i < offset; i++) {
        count*=2;
    }

    int x = bitmap[block] & (int)(128/count);
    if(x != 0)
            return 1;
    return 0;
}

void set_physical_mem() {

    //Allocate physical memory using mmap or malloc; this is the total size of
    //your memory you are simulating
    	initialize = 1;
	heap = malloc(MEMSIZE);
	memset(heap, 0, MEMSIZE);

	numPhysPages = MEMSIZE / PGSIZE; //number of pages in (1GB) physical memory
	physBitMapChars = numPhysPages / 8; //number of chars needed for physical memory bitmap (1 bit per page)
	pbitmap = malloc(physBitMapChars); //physcial bitmap
	memset(pbitmap, 0, physBitMapChars);

	numVirtPages = MAX_MEMSIZE / PGSIZE; //number of pages in (4GB) virt addr space
	virtBitMapChars = numVirtPages / 8; //number of chars needed for virt addr bitmap (1 bit per page)
	vbitmap = malloc(virtBitMapChars); //virtual bitmap
	memset(vbitmap, 0, virtBitMapChars);

//	int tlbitmapChars = TLB_ENTRIES / 8; 
//	tlbitmap = malloc(tlbitmapChars);
//	memset(tlbitmap, 0, tlbitmapChars);

	numOffsetBits = log2(PGSIZE);
	int VPNbits = 64-numOffsetBits;
	if(VPNbits % 4 == 0) {
		numPD1bits = VPNbits/4;
		numPD2bits = VPNbits/4;
		numPD3bits = VPNbits/4;
		numPTbits = VPNbits/4;
	}
	else if(VPNbits % 4 == 1) {
                numPD1bits = VPNbits/4 + 1;
                numPD2bits = VPNbits/4;
                numPD3bits = VPNbits/4;
                numPTbits = VPNbits/4;
        }
	else if(VPNbits % 4 == 2) {
                numPD1bits = VPNbits/4 + 1;
                numPD2bits = VPNbits/4 + 1;
                numPD3bits = VPNbits/4;
                numPTbits = VPNbits/4;
        }
	else if(VPNbits % 4 == 3) {
                numPD1bits = VPNbits/4 + 1;
                numPD2bits = VPNbits/4 + 1;
                numPD3bits = VPNbits/4 + 1;
                numPTbits = VPNbits/4;
        }
	
	PD = (unsigned long long ****) heap; //outer-level page directory takes up the first page of our "fake" physical memory (PD[0] ... PD[1023] are the PDEs of this page directory - for 4K pages) 
//From here, we can go PD[0] = t_malloc(PGSIZE) to allocate a page for the inner level page table that PD[0] points to
//our t_malloc function will return an address heap+n*PGSIZE based on the next free physical frame/page available (where n is the nth page in physical memory where our allocation will be stored. In this case, PD[0] is an unsigned long pointer, and so we simply cast the address returned by t_malloc to (unsigned long *) CORRECTION: we will not use t_malloc to allocate page table pages, we will instead do this directly: PD[0] = (unsigned long *) heap+n*PGSIZE where n is next physical page available. This is beacuse t_malloc will allocate virtual pages which is not necessary for page table (page table not in virtual address space, only in physical memory (our "fake" physical memory))	
	pbitmap[0] = pbitmap[0] | 128; //first page of physical memory will be reserved for outer page directory
	vbitmap[0] = vbitmap[0] | 128; //mark first page of vbitmap as allocated (so that we do not have to return a 0 address)
	
	for(int i = 0; i < TLB_ENTRIES; i++) {
		tlb[i].VPN = 0;
		tlb[i].physAddr = 0;
	}
    
    //HINT: Also calculate the number of physical and virtual pages and allocate
    //virtual and physical bitmaps and initialize them

}


/*
 * Part 2: Add a virtual to physical page translation to the TLB.
 * Feel free to extend the function arguments or return type.
 */
int
add_TLB(void *va, void *pa)
{

    /*Part 2 HINT: Add a virtual to physical page translation to the TLB */
//look for a free spot in TLB, if none then evict something and put new translation in free TLB entry
	//depending on what va and pa are returned (whether va and pa are base addresses of their respective pages, or they are
	//specific addresses inside their respective pages, we might have to subtract the offset from both
	unsigned long long int mask = 1;
        for(int i = 0; i < numOffsetBits; i++) {
                mask*=2;
        }
        mask-=1;
        unsigned long long int offset = (unsigned long long int)va & mask;
        unsigned long long int VPN = (unsigned long long int)va >> numOffsetBits;
	unsigned long long int physAddr = (unsigned long long int)pa - offset;
	int num = VPN%TLB_ENTRIES;
	tlb[num].VPN = VPN;
	tlb[num].physAddr = physAddr;

    return -1;
}


/*
 * Part 2: Check TLB for a valid translation.
 * Returns the physical page address.
 * Feel free to extend this function and change the return type.
 */
void *
check_TLB(void *va) { //changed return type from pte_t * to void *

    /* Part 2: TLB lookup code here */
	tlbChecks++;
	unsigned long long int mask = 1;
	for(int i = 0; i < numOffsetBits; i++) {
		mask*=2;
	}
	mask-=1;
	unsigned long long int offset = (unsigned long long int)va & mask;
        unsigned long long int VPN = (unsigned long long int)va >> numOffsetBits;
	//search TLB for VPN
	unsigned long long int physAddr = 0;
	int TLBindex = VPN%TLB_ENTRIES;
	if(tlb[TLBindex].VPN == VPN)
		physAddr = tlb[TLBindex].physAddr;
	else {
		tlbMisses++;
		return NULL;
	}
	physAddr+=offset;
	return (void *)physAddr;
	//get physical address associated with VPN
	//add offset that we calculated
	//return phys addr	
}


/*
 * Part 2: Print TLB miss rate.
 * Feel free to extend the function arguments or return type.
 */
void
print_TLB_missrate()
{
    double miss_rate = 0;	

    /*Part 2 Code here to calculate and print the TLB miss rate*/
    if(tlbMisses == 0)
	miss_rate = 0;
    else
	miss_rate = (double)tlbMisses/tlbChecks;


    fprintf(stderr, "TLB miss rate %lf \n", miss_rate);
}



/*
The function takes a virtual address and page directories starting address and
performs translation to return the physical address
*/
void *translate(pde_t ****pgdir, void *va) { //changed return type from pte_t * to void *
    /* Part 1 HINT: Get the Page directory index (1st level) Then get the
    * 2nd-level-page table index using the virtual address.  Using the page
    * directory index and page table index get the physical address.
    *
    * Part 2 HINT: Check the TLB before performing the translation. If
    * translation exists, then you can return physical address from the TLB.
    */
	void *check = check_TLB(va);
	if(check != NULL)
		return check;
	unsigned long long int virtaddr = (unsigned long long int)va; //cast va to unsigned long
	unsigned long long int VPN = virtaddr >> numOffsetBits;
	unsigned long long int PD1index = VPN >> (numPD2bits+numPD3bits+numPTbits);
	unsigned long long int x = 1;
	for(int i = 0; i < numPTbits; i++) {
		x*=2;
	}
	x-=1;
	unsigned long long int PTindex = VPN & x;
	VPN = VPN >> numPTbits;
	x = 1;
        for(int i = 0; i < numPD3bits; i++) {
                x*=2;
        }
        x-=1;
        unsigned long long int PD3index = VPN & x;
	VPN = VPN >> numPD3bits;
	x = 1;
        for(int i = 0; i < numPD2bits; i++) {
                x*=2;
        }
        x-=1;
        unsigned long long int PD2index = VPN & x;

	if(PD[PD1index] == NULL)
		return NULL;
	//will we ever try to translate addresses from pages that we previously freed?
	if(PD[PD1index][PD2index] == NULL)
		return NULL;
	if(PD[PD1index][PD2index][PD3index] == NULL)
		return NULL;
	if(PD[PD1index][PD2index][PD3index][PTindex] == 0)
                return NULL;
	unsigned long long int physAddr = PD[PD1index][PD2index][PD3index][PTindex];
	//since we could return the physical address itself, should we return it as an unsigned long or void * (probably void *)
	x = 1;
	for(int i = 0; i < numOffsetBits; i++) {
		x*=2;
	}
	x-=1;
	x = virtaddr & x;
	physAddr += x;
	add_TLB(va, (void *)physAddr);

    //If translation not successful, then return NULL
    return (void *)physAddr; 
}

void *next_avail_phys_page() {
	for(unsigned long long int i = 0; i < numPhysPages; i++) {
		if(get_bit_at_index(pbitmap, i) == 0) {
			set_bit_at_index(pbitmap, i);
			return (heap+(i*PGSIZE));
		}
	}
	return NULL;
}

/*
The function takes a page directory address, virtual address, physical address
as an argument, and sets a page table entry. This function will walk the page
directory to see if there is an existing mapping for a virtual address. If the
virtual address is not present, then a new entry will be added
*/
int
page_map(pde_t ****pgdir, void *va, void *pa)
{

    /*HINT: Similar to translate(), find the page directory (1st level)
    and page table (2nd-level) indices. If no mapping exists, set the
    virtual to physical mapping */
	unsigned long long int virtaddr = (unsigned long long int)va; //cast va to unsigned long
        unsigned long long int VPN = virtaddr >> numOffsetBits;
        unsigned long long int PD1index = VPN >> (numPD2bits+numPD3bits+numPTbits);
        unsigned long long int x = 1;
        for(int i = 0; i < numPTbits; i++) {
                x*=2;
        }
        x-=1;
        unsigned long long int PTindex = VPN & x;
        VPN = VPN >> numPTbits;
        x = 1;
        for(int i = 0; i < numPD3bits; i++) {
                x*=2;
        }
        x-=1;
        unsigned long long int PD3index = VPN & x;
        VPN = VPN >> numPD3bits;
        x = 1;
        for(int i = 0; i < numPD2bits; i++) {
                x*=2;
        }
        x-=1;
        unsigned long long int PD2index = VPN & x;

	if(PD[PD1index] == NULL) {
		PD[PD1index] = (unsigned long long int ***)next_avail_phys_page();// memcpy(PD[PDindex], nextavailphyspage(), PGSIZE)
		if(PD[PD1index] == NULL) //error check to see if there are not enough physical pages in phys. memory to allocate space for inner level page table
			return 0;
	}
	if(PD[PD1index][PD2index] == NULL) {
                PD[PD1index][PD2index] = (unsigned long long int **)next_avail_phys_page();// memcpy(PD[PDindex], nextavailphyspage(), PGSIZE)
                if(PD[PD1index][PD2index] == NULL) //error check to see if there are not enough physical pages in phys. memory to allocate space for inner level page table
                        return 0;
        }
	if(PD[PD1index][PD2index][PD3index] == NULL) {
                PD[PD1index][PD2index][PD3index] = (unsigned long long int *)next_avail_phys_page();// memcpy(PD[PDindex], nextavailphyspage(), PGSIZE)
                if(PD[PD1index][PD2index] == NULL) //error check to see if there are not enough physical pages in phys. memory to allocate space for inner level page table
                        return 0;
        }

	PD[PD1index][PD2index][PD3index][PTindex] = (unsigned long long int)(pa);//(unsigned long int)next_avail_phys_page();
	if(PD[PD1index][PD2index][PD3index][PTindex] == 0)
		return 0;
	tlbChecks++;
	tlbMisses++;
	add_TLB(va, pa); //comment add to TLB

	return -1;
}


/*Function that gets the next available page
*/
void *get_next_avail(int num_pages) {
 
    //Use virtual address bitmap to find the next free page
	int count = 0;
	unsigned long long int startIndex;
	unsigned long long int virtAddr;
	for(unsigned long long int i = 0; i < numVirtPages; i++) {
		if(get_bit_at_index(vbitmap, i) == 0) {
		      if(count == 0)
			      startIndex = i;
		      if(++count == num_pages) {
			      virtAddr = startIndex*PGSIZE; //VA: start of allocation of virtual pages (base address of the first virtual page of this allocation) in the virtual addr space
		              for(int j = 0; j < num_pages; j++) {
				      set_bit_at_index(vbitmap, startIndex+j);
			      }
			      return (void *)virtAddr;
		      }
		}
		else
			count = 0;
	}
	return NULL;
}


/* Function responsible for allocating pages
and used by the benchmark
*/
void *t_malloc(unsigned int num_bytes) {

    /* 
     * HINT: If the physical memory is not yet initialized, then allocate and initialize.
     */
pthread_mutex_lock(&mutex);	
	if(initialize == 0) {
		set_physical_mem();
		initialize = 1;
	}
   /* 
    * HINT: If the page directory is not initialized, then initialize the
    * page directory. Next, using get_next_avail(), check if there are free pages. If
    * free pages are available, set the bitmaps and map a new page. Note, you will 
    * have to mark which physical pages are used. 
    */
	unsigned int numPages = num_bytes/PGSIZE;
	if(num_bytes % PGSIZE != 0)
		numPages++;
	unsigned long long int physAddr;
	unsigned long long int virtAddr = (unsigned long long int)(get_next_avail(numPages));
	unsigned long long int mapVirtAddr = virtAddr;
	for(int i = 0; i < numPages; i++) {
		physAddr = (unsigned long long int)next_avail_phys_page();
		if(physAddr == 0)
			return NULL; //not enough physical space
		page_map(PD, (void *)mapVirtAddr, (void *)physAddr);
		mapVirtAddr+=PGSIZE;
	}
	
pthread_mutex_unlock(&mutex);
	return (void *)virtAddr;

}

/* Responsible for releasing one or more memory pages using virtual address (va)
*/
void t_free(void *va, int size) {

    /* Part 1: Free the page table entries starting from this virtual address
     * (va). Also mark the pages free in the bitmap. Perform free only if the 
     * memory from "va" to va+size is valid.
     *
     * Part 2: Also, remove the translation from the TLB
     */
	//use translate to get phys addr of virt addr. subtract heap from phys addr. right shift to get PFN, and this will be the index we have to set free add PGSIZE to OG va to free all other phys pages as well
	//right shift va offset bits and this will be the starting page we have to free. Do size/PGSIZE to get how many pages we must free (add 1 if % == 1). add one to VPN after right shifting to get other indices we must set free for vbitmap
	pthread_mutex_lock(&mutex);
	unsigned int numPages = size/PGSIZE;
	if(size % PGSIZE != 0)
		numPages++;
	unsigned long long int physAddr;
	unsigned long long int virtAddr = (unsigned long long int)va;
	for(int i = 0; i < numPages; i++) {
		void *temp = translate(PD, (void *)virtAddr);
		if(temp == NULL)
			return;
		physAddr = (unsigned long long int)temp;
		physAddr-=(unsigned long long int)heap;
		unsigned long long int PFN = physAddr >> numOffsetBits;
		free_bit_at_index(pbitmap, PFN);
		unsigned long long int VPN = (virtAddr >> numOffsetBits);
		free_bit_at_index(vbitmap, VPN);
		int TLBindex = VPN%TLB_ENTRIES;
		if(tlb[TLBindex].VPN == VPN) {
			tlb[TLBindex].VPN = 0;
			tlb[TLBindex].physAddr = 0;
		}
		virtAddr+=PGSIZE;
	}
	pthread_mutex_unlock(&mutex);	
	return;
}


/* The function copies data pointed by "val" to physical
 * memory pages using virtual address (va)
 * The function returns 0 if the put is successfull and -1 otherwise.
*/
void put_value(void *va, void *val, int size) {

    /* HINT: Using the virtual address and translate(), find the physical page. Copy
     * the contents of "val" to a physical page. NOTE: The "size" value can be larger 
     * than one page. Therefore, you may have to find multiple pages using translate()
     * function.
     */	
	unsigned long long int virtAddr = (unsigned long long int)va;
	unsigned long long int x = 1;
	for(int i = 0; i < numOffsetBits; i++) {
		x*=2;
	}
	x-=1;
	unsigned long long int offset = virtAddr & x;
	int fsize;
	pthread_mutex_lock(&mutex);
	void *pa = (translate(PD, va));
	pthread_mutex_unlock(&mutex);
        if(size >= (PGSIZE-offset))
		fsize = PGSIZE - offset;
	else {
		memcpy(pa, val, size);
		return;
	}

	memcpy(pa, val, fsize);
	size-=fsize;
	virtAddr+=fsize;
	val+=fsize;
	//check if virt pages are allocated using VPN as index to check vbitmap
	int numPages = size/PGSIZE;
        int remainder;
        if((remainder = size%PGSIZE) != 0)
                numPages++;
	for(int i = 0; i < numPages; i++) {
		pthread_mutex_lock(&mutex);
                pa = (translate(PD, (void *)virtAddr));
		pthread_mutex_unlock(&mutex);
                if(i == numPages - 1 && remainder != 0) {
                        memcpy(pa, val, remainder);
			return;
		}
                else
                        memcpy(pa, val, PGSIZE);
                virtAddr += PGSIZE;
                val+=PGSIZE;
        }
//	pthread_mutex_unlock(&mutex);
	return;
}


/*Given a virtual address, this function copies the contents of the page to val*/
void get_value(void *va, void *val, int size) {

    /* HINT: put the values pointed to by "va" inside the physical memory at given
    * "val" address. Assume you can access "val" directly by derefencing them.
    */
	unsigned long long int virtAddr = (unsigned long long int)va;
        unsigned long long int x = 1;
        for(int i = 0; i < numOffsetBits; i++) {
                x*=2;
        }
        x-=1;
        unsigned long long int offset = virtAddr & x;
        int fsize;
	pthread_mutex_lock(&mutex);
        void *pa = (translate(PD, va));
	pthread_mutex_unlock(&mutex);
        if(size >= (PGSIZE-offset))
                fsize = PGSIZE - offset;
        else {
                memcpy(val, pa, size);
                return;
        }

        memcpy(val, pa, fsize);
        size-=fsize;
        virtAddr+=fsize;
        val+=fsize;
        //check if virt pages are allocated using VPN as index to check vbitmap
        int numPages = size/PGSIZE;
        int remainder;
        if((remainder = size%PGSIZE) != 0)
                numPages++;
        for(int i = 0; i < numPages; i++) {
		pthread_mutex_lock(&mutex);
                pa = (translate(PD, (void *)virtAddr));
		pthread_mutex_unlock(&mutex);
                if(i == numPages - 1 && remainder != 0) {
                        memcpy(val, pa, remainder);
                        return;
                }
                else
                        memcpy(val, pa, PGSIZE);
                virtAddr += PGSIZE;
                val+=PGSIZE;
        }
	return;
}

/*
This function receives two matrices mat1 and mat2 as an argument with size
argument representing the number of rows and columns. After performing matrix
multiplication, copy the result to answer.
*/
void mat_mult(void *mat1, void *mat2, int size, void *answer) {

    /* Hint: You will index as [i * size + j] where  "i, j" are the indices of the
     * matrix accessed. Similar to the code in test.c, you will use get_value() to
     * load each element and perform multiplication. Take a look at test.c! In addition to 
     * getting the values from two matrices, you will perform multiplication and 
     * store the result to the "answer array"
     */
    int x, y, val_size = sizeof(int);
    int i, j, k;
    for (i = 0; i < size; i++) {
        for(j = 0; j < size; j++) {
            unsigned int a, b, c = 0;
            for (k = 0; k < size; k++) {
                int address_a = (unsigned int)mat1 + ((i * size * sizeof(int))) + (k * sizeof(int));
                int address_b = (unsigned int)mat2 + ((k * size * sizeof(int))) + (j * sizeof(int));
                get_value( (void *)address_a, &a, sizeof(int));
                get_value( (void *)address_b, &b, sizeof(int));
                // printf("Values at the index: %d, %d, %d, %d, %d\n", 
                //     a, b, size, (i * size + k), (k * size + j));
                c += (a * b);
            }
            int address_c = (unsigned int)answer + ((i * size * sizeof(int))) + (j * sizeof(int));
            // printf("This is the c: %d, address: %x!\n", c, address_c);
            put_value((void *)address_c, (void *)&c, sizeof(int));
        }
    }
}



