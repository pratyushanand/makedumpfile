/*
 * arch/arm64.c : Based on arch/arm.c
 *
 * Copyright (C) 2015 Red Hat, Pratyush Anand <panand@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation (version 2 of the License).
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef __aarch64__

#include "../elf_info.h"
#include "../makedumpfile.h"
#include "../print_info.h"

typedef struct {
	unsigned long pgd;
} pgd_t;

typedef struct {
	pgd_t pgd;
} pud_t;

typedef struct {
	pud_t pud;
} pmd_t;

#define pud_offset(pgd, vaddr) 	((pud_t *)pgd)

#define pgd_val(x)		((x).pgd)
#define pud_val(x)		(pgd_val((x).pgd))
#define pmd_val(x)		(pud_val((x).pud))

#define PUD_SHIFT		PGDIR_SHIFT
#define PUD_SIZE		(1UL << PUD_SHIFT)

typedef struct {
	unsigned long pte;
} pte_t;
#define pte_val(x)		((x).pte)

#define PAGE_SIZE		(1UL << PAGE_SHIFT)
#define PAGE_MASK		(~(PAGE_SIZE - 1))
#define PGDIR_SHIFT		((PAGE_SHIFT - 3) * ARM64_PGTABLE_LEVELS + 3)
#define PTRS_PER_PGD		(1 << (VA_BITS - PGDIR_SHIFT))
#define PMD_SHIFT		((PAGE_SHIFT - 3) * 2 + 3)
#define PTRS_PER_PTE		(1 << (PAGE_SHIFT - 3))
#define PMD_SHIFT		((PAGE_SHIFT - 3) * 2 + 3)
#define PMD_SIZE		(1UL << PMD_SHIFT)
#define PMD_MASK		(~(PMD_SIZE - 1))
#define PTRS_PER_PMD		PTRS_PER_PTE

#define PAGE_PRESENT		(1 << 0)
#define SECTIONS_SIZE_BITS	30
/*

* Highest possible physical address supported.
*/
#define PHYS_MASK_SHIFT		48
#define PHYS_MASK		((1UL << PHYS_MASK_SHIFT) - 1)
/*
 * Remove the highest order bits that are not a part of the
 * physical address in a section
 */
#define PMD_SECTION_MASK        ((1UL << 40) - 1)

#define PMD_TYPE_MASK		3
#define PMD_TYPE_SECT		1
#define PMD_TYPE_TABLE		3

#define __va(paddr) 			((paddr) - info->phys_base + PAGE_OFFSET)
#define __pa(vaddr) 			((vaddr) - PAGE_OFFSET + info->phys_base)

#define pgd_index(vaddr) 		(((vaddr) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))
#define pgd_offset(pgdir, vaddr)	((pgd_t *)(pgdir) + pgd_index(vaddr))

#define pte_index(addr) 		(((addr) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pmd_page_vaddr(pmd)		(__va(pmd_val(pmd) & PHYS_MASK & (int32_t)PAGE_MASK))
#define pte_offset(dir, vaddr) 		((pte_t*)pmd_page_vaddr((*dir)) + pte_index(vaddr))


#define pmd_offset_pgtbl_lvl_2(pud, vaddr) ((pmd_t *)pud)

#define pmd_index(vaddr)		(((vaddr) >> PMD_SHIFT) & (PTRS_PER_PMD - 1))
#define pud_page_vaddr(pud)		(__va(pud_val(pud) & PHYS_MASK & (int32_t)PAGE_MASK))
#define pmd_offset_pgtbl_lvl_3(pud, vaddr) ((pmd_t *)pud_page_vaddr((*pud)) + pmd_index(vaddr))

/* kernel struct page size can be kernel version dependent, currently
 * keep it constant.
 */
#define KERN_STRUCT_PAGE_SIZE		get_structure_size("page", DWARF_INFO_GET_STRUCT_SIZE)

#define ALIGN(x, a) 			(((x) + (a) - 1) & ~((a) - 1))
#define PFN_DOWN(x)			((x) >> PAGE_SHIFT)
#define VMEMMAP_SIZE			ALIGN((1UL << (VA_BITS - PAGE_SHIFT)) * KERN_STRUCT_PAGE_SIZE, PUD_SIZE)
#define MODULES_END			PAGE_OFFSET
#define MODULES_VADDR			(MODULES_END - 0x4000000)

static int pgtable_level;
static int va_bits;
static int page_shift;

int
get_pgtable_level_arm64(void)
{
	return pgtable_level;
}

int
get_va_bits_arm64(void)
{
	return va_bits;
}

int
get_page_shift_arm64(void)
{
	return page_shift;
}

pmd_t *
pmd_offset(pud_t *pud, unsigned long vaddr)
{
	if (pgtable_level == 2) {
		return pmd_offset_pgtbl_lvl_2(pud, vaddr);
	} else {
		return pmd_offset_pgtbl_lvl_3(pud, vaddr);
	}
}

#define PAGE_OFFSET_39 (0xffffffffffffffffUL << 39)
#define PAGE_OFFSET_42 (0xffffffffffffffffUL << 42)
static int calculate_plat_config(void)
{
	unsigned long long stext;

	/* Currently we assume that there are only two possible
	 * configuration supported by kernel.
	 * 1) Page Table Level:2, Page Size 64K and VA Bits 42
	 * 1) Page Table Level:3, Page Size 4K and VA Bits 39
	 * Ideally, we should have some mechanism to decide these values
	 * from kernel symbols, but we have limited symbols in vmcore,
	 * and we can not do much. So until some one comes with a better
	 * way, we use following.
	 */
	stext = SYMBOL(_stext);

	/* condition for minimum VA bits must be checked first and so on */
	if ((stext & PAGE_OFFSET_39) == PAGE_OFFSET_39) {
		pgtable_level = 3;
		va_bits = 39;
		page_shift = 12;
	} else if ((stext & PAGE_OFFSET_42) == PAGE_OFFSET_42) {
		pgtable_level = 2;
		va_bits = 42;
		page_shift = 16;
	} else {
		ERRMSG("Kernel Configuration not supported\n");
		return FALSE;
	}

	return TRUE;
}

static int
is_vtop_from_page_table_arm64(unsigned long vaddr)
{
	/* If virtual address lies in vmalloc, vmemmap or module space
	 * region then, get the physical address from page table.
	 */
	return ((vaddr >= VMALLOC_START && vaddr <= VMALLOC_END)
		|| (vaddr >= VMEMMAP_START && vaddr <= VMEMMAP_END)
		|| (vaddr >= MODULES_VADDR && vaddr <= MODULES_END));
}

int
get_phys_base_arm64(void)
{
	unsigned long phys_base = ULONG_MAX;
	unsigned long long phys_start;
	int i;

	if (!calculate_plat_config()) {
		ERRMSG("Can't determine platform config values\n");
		return FALSE;
	}

	/*
	 * We resolve phys_base from PT_LOAD segments. LMA contains physical
	 * address of the segment, and we use the lowest start as
	 * phys_base.
	 */
	for (i = 0; get_pt_load(i, &phys_start, NULL, NULL, NULL); i++) {
		if (phys_start < phys_base)
			phys_base = phys_start;
	}

	if (phys_base == ULONG_MAX) {
		ERRMSG("Can't determine phys_base\n");
		return FALSE;
	}

	info->phys_base = phys_base;

	DEBUG_MSG("phys_base    : %lx\n", phys_base);

	return TRUE;
}

int
get_machdep_info_arm64(void)
{
	info->max_physmem_bits = PHYS_MASK_SHIFT;
	info->section_size_bits = SECTIONS_SIZE_BITS;
	info->page_offset = SYMBOL(_stext)
		& (0xffffffffffffffffUL << (VA_BITS - 1));
	info->vmalloc_start = 0xffffffffffffffffUL << VA_BITS;
	info->vmalloc_end = PAGE_OFFSET - PUD_SIZE - VMEMMAP_SIZE - 0x10000;
	info->vmemmap_start = VMALLOC_END + 0x10000;
	info->vmemmap_end = VMEMMAP_START + VMEMMAP_SIZE;

	DEBUG_MSG("max_physmem_bits : %lx\n", info->max_physmem_bits);
	DEBUG_MSG("section_size_bits: %lx\n", info->section_size_bits);
	DEBUG_MSG("page_offset      : %lx\n", info->page_offset);
	DEBUG_MSG("vmalloc_start    : %lx\n", info->vmalloc_start);
	DEBUG_MSG("vmalloc_end      : %lx\n", info->vmalloc_end);
	DEBUG_MSG("vmemmap_start    : %lx\n", info->vmemmap_start);
	DEBUG_MSG("vmemmap_end      : %lx\n", info->vmemmap_end);
	DEBUG_MSG("modules_start    : %lx\n", MODULES_VADDR);
	DEBUG_MSG("modules_end      : %lx\n", MODULES_END);

	return TRUE;
}

unsigned long long
kvtop_xen_arm64(unsigned long kvaddr)
{
	return ERROR;
}

int
get_xen_basic_info_arm64(void)
{
	return ERROR;
}

int
get_xen_info_arm64(void)
{
	return ERROR;
}

int
get_versiondep_info_arm64(void)
{
	return TRUE;
}

/*
 * vtop_arm64() - translate arbitrary virtual address to physical
 * @vaddr: virtual address to translate
 *
 * Function translates @vaddr into physical address using page tables. This
 * address can be any virtual address. Returns physical address of the
 * corresponding virtual address or %NOT_PADDR when there is no translation.
 */
static unsigned long long
vtop_arm64(unsigned long vaddr)
{
	unsigned long long paddr = NOT_PADDR;
	pgd_t	*pgda, pgdv;
	pud_t	pudv;
	pmd_t	*pmda, pmdv;
	pte_t 	*ptea, ptev;

	if (SYMBOL(swapper_pg_dir) == NOT_FOUND_SYMBOL) {
		ERRMSG("Can't get the symbol of swapper_pg_dir.\n");
		return NOT_PADDR;
	}

	pgda = pgd_offset(SYMBOL(swapper_pg_dir), vaddr);
	if (!readmem(VADDR, (unsigned long long)pgda, &pgdv, sizeof(pgdv))) {
		ERRMSG("Can't read pgd\n");
		return NOT_PADDR;
	}

	pudv.pgd = pgdv;

	pmda = pmd_offset(&pudv, vaddr);
	if (!readmem(VADDR, (unsigned long long)pmda, &pmdv, sizeof(pmdv))) {
		ERRMSG("Can't read pmd\n");
		return NOT_PADDR;
	}

	switch (pmd_val(pmdv) & PMD_TYPE_MASK) {
	case PMD_TYPE_TABLE:
		ptea = pte_offset(&pmdv, vaddr);
		/* 64k page */
		if (!readmem(VADDR, (unsigned long long)ptea, &ptev, sizeof(ptev))) {
			ERRMSG("Can't read pte\n");
			return NOT_PADDR;
		}

		if (!(pte_val(ptev) & PAGE_PRESENT)) {
			ERRMSG("Can't get a valid pte.\n");
			return NOT_PADDR;
		} else {

			paddr = (PAGEBASE(pte_val(ptev)) & PHYS_MASK)
					+ (vaddr & (PAGESIZE() - 1));
		}
		break;
	case PMD_TYPE_SECT:
		/* 1GB section */
		paddr = (pmd_val(pmdv) & (PMD_MASK & PMD_SECTION_MASK))
					+ (vaddr & (PMD_SIZE - 1));
		break;
	}

	return paddr;
}

unsigned long long
vaddr_to_paddr_arm64(unsigned long vaddr)
{
	/*
	 * use translation tables when a) user has explicitly requested us to
	 * perform translation for a given address. b) virtual address lies in
	 * vmalloc, vmemmap or modules memory region. Otherwise we assume that
	 * the translation is done within the kernel direct mapped region.
	 */
	if ((info->vaddr_for_vtop == vaddr) ||
			is_vtop_from_page_table_arm64(vaddr))
		return vtop_arm64(vaddr);

	return __pa(vaddr);
}
#endif /* __aarch64__ */
