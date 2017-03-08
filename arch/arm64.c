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

typedef struct {
	unsigned long pte;
} pte_t;

static int pgtable_level;
static int va_bits;
static unsigned long kimage_voffset;

#define SZ_4K			(4 * 1024)
#define SZ_16K			(16 * 1024)
#define SZ_64K			(64 * 1024)
#define SZ_128M			(128 * 1024 * 1024)

#define pgd_val(x)		((x).pgd)
#define pud_val(x)		(pgd_val((x).pgd))
#define pmd_val(x)		(pud_val((x).pud))
#define pte_val(x)		((x).pte)

#define PAGE_MASK		(~(PAGESIZE() - 1))
#define PGDIR_SHIFT		((PAGESHIFT() - 3) * pgtable_level + 3)
#define PTRS_PER_PGD		(1 << (va_bits - PGDIR_SHIFT))
#define PUD_SHIFT		get_pud_shift_arm64()
#define PTRS_PER_PTE		(1 << (PAGESHIFT() - 3))
#define PTRS_PER_PUD		PTRS_PER_PTE
#define PMD_SHIFT		((PAGESHIFT() - 3) * 2 + 3)
#define PMD_SIZE		(1UL << PMD_SHIFT)
#define PMD_MASK		(~(PMD_SIZE - 1))
#define PTRS_PER_PMD		PTRS_PER_PTE

#define PAGE_PRESENT		(1 << 0)
#define SECTIONS_SIZE_BITS	30
/* Highest possible physical address supported */
#define PHYS_MASK_SHIFT		48
#define PHYS_MASK		((1UL << PHYS_MASK_SHIFT) - 1)
/*
 * Remove the highest order bits that are not a part of the
 * physical address in a section
 */
#define PMD_SECTION_MASK	((1UL << 40) - 1)

#define PMD_TYPE_MASK		3
#define PMD_TYPE_SECT		1
#define PMD_TYPE_TABLE		3

#define pgd_index(vaddr) 		(((vaddr) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))
#define pgd_offset(pgdir, vaddr)	((pgd_t *)(pgdir) + pgd_index(vaddr))

#define pte_index(vaddr) 		(((vaddr) >> PAGESHIFT()) & (PTRS_PER_PTE - 1))
#define pmd_page_paddr(pmd)		(pmd_val(pmd) & PHYS_MASK & (int32_t)PAGE_MASK)
#define pte_offset(dir, vaddr) 		((pte_t*)pmd_page_paddr((*dir)) + pte_index(vaddr))

#define pmd_index(vaddr)		(((vaddr) >> PMD_SHIFT) & (PTRS_PER_PMD - 1))
#define pud_page_paddr(pud)		(pud_val(pud) & PHYS_MASK & (int32_t)PAGE_MASK)
#define pmd_offset_pgtbl_lvl_2(pud, vaddr) ((pmd_t *)pud)
#define pmd_offset_pgtbl_lvl_3(pud, vaddr) ((pmd_t *)pud_page_paddr((*pud)) + pmd_index(vaddr))

#define pud_index(vaddr)		(((vaddr) >> PUD_SHIFT) & (PTRS_PER_PUD - 1))
#define pgd_page_paddr(pgd)		(pgd_val(pgd) & PHYS_MASK & (int32_t)PAGE_MASK)

static unsigned long long
__pa(unsigned long vaddr)
{
	if (kimage_voffset == NOT_FOUND_NUMBER ||
			(vaddr >= PAGE_OFFSET))
		return (vaddr - PAGE_OFFSET + info->phys_base);
	else
		return (vaddr - kimage_voffset);
}

static int
get_pud_shift_arm64(void)
{
	if (pgtable_level == 4)
		return ((PAGESHIFT() - 3) * 3 + 3);
	else
		return PGDIR_SHIFT;
}

static pmd_t *
pmd_offset(pud_t *puda, pud_t *pudv, unsigned long vaddr)
{
	if (pgtable_level == 2) {
		return pmd_offset_pgtbl_lvl_2(puda, vaddr);
	} else {
		return pmd_offset_pgtbl_lvl_3(pudv, vaddr);
	}
}

static pud_t *
pud_offset(pgd_t *pgda, pgd_t *pgdv, unsigned long vaddr)
{
	if (pgtable_level == 4)
		return ((pud_t *)pgd_page_paddr((*pgdv)) + pud_index(vaddr));
	else
		return (pud_t *)(pgda);
}

static int calculate_plat_config(void)
{
	va_bits = NUMBER(VA_BITS);

	/*
	 * RHELSA7.3 only, because that has older version of kdump patches
	 * which does not have va_bits in vmcoreinfo
	 */
	if (va_bits == NOT_FOUND_NUMBER)
		va_bits = 42;

	/* derive pgtable_level as per arch/arm64/Kconfig */
	if ((PAGESIZE() == SZ_16K && va_bits == 36) ||
			(PAGESIZE() == SZ_64K && va_bits == 42)) {
		pgtable_level = 2;
	} else if ((PAGESIZE() == SZ_64K && va_bits == 48) ||
			(PAGESIZE() == SZ_4K && va_bits == 39) ||
			(PAGESIZE() == SZ_16K && va_bits == 47)) {
		pgtable_level = 3;
	} else if ((PAGESIZE() != SZ_64K && va_bits == 48)) {
		pgtable_level = 4;
	} else {
		ERRMSG("PAGE SIZE %#lx and VA Bits %d not supported\n",
				PAGESIZE(), va_bits);
		return FALSE;
	}

	return TRUE;
}

unsigned long
get_kvbase_arm64(void)
{
	return (0xffffffffffffffffUL << va_bits);
}

int
get_phys_base_arm64(void)
{
	unsigned long phys_base = ULONG_MAX;
	unsigned long long phys_start;
	int i;

	info->phys_base = NUMBER(PHYS_OFFSET);
	if (info->phys_base == NOT_FOUND_NUMBER) {
		/*
		 * RHELSA7.3 only, because that has older version of kdump
		 * patches which does not have PHYS_OFFSET in vmcoreinfo
		 */
		for (i = 0; get_pt_load(i, &phys_start,
					NULL, NULL, NULL); i++) {
			if (phys_start < phys_base)
				phys_base = phys_start;
		}

		if (phys_base == ULONG_MAX) {
			ERRMSG("Can't determine phys_base\n");
			return FALSE;
		}
		info->phys_base = phys_base;
	}

	DEBUG_MSG("phys_base    : %lx\n", info->phys_base);

	return TRUE;
}

int
get_machdep_info_arm64(void)
{
	if (!calculate_plat_config()) {
		ERRMSG("Can't determine platform config values\n");
		return FALSE;
	}

	kimage_voffset = NUMBER(kimage_voffset);
	info->max_physmem_bits = PHYS_MASK_SHIFT;
	info->section_size_bits = SECTIONS_SIZE_BITS;
	info->page_offset = 0xffffffffffffffffUL << (va_bits - 1);

	DEBUG_MSG("kimage_voffset   : %lx\n", kimage_voffset);
	DEBUG_MSG("max_physmem_bits : %lx\n", info->max_physmem_bits);
	DEBUG_MSG("section_size_bits: %lx\n", info->section_size_bits);
	DEBUG_MSG("page_offset      : %lx\n", info->page_offset);

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
 * vaddr_to_paddr_arm64() - translate arbitrary virtual address to physical
 * @vaddr: virtual address to translate
 *
 * Function translates @vaddr into physical address using page tables. This
 * address can be any virtual address. Returns physical address of the
 * corresponding virtual address or %NOT_PADDR when there is no translation.
 */
unsigned long long
vaddr_to_paddr_arm64(unsigned long vaddr)
{
	unsigned long long paddr = NOT_PADDR;
	unsigned long long swapper_phys;
	pgd_t	*pgda, pgdv;
	pud_t	*puda, pudv;
	pmd_t	*pmda, pmdv;
	pte_t 	*ptea, ptev;

	if (SYMBOL(swapper_pg_dir) == NOT_FOUND_SYMBOL) {
		ERRMSG("Can't get the symbol of swapper_pg_dir.\n");
		return NOT_PADDR;
	}

	swapper_phys = __pa(SYMBOL(swapper_pg_dir));

	pgda = pgd_offset(swapper_phys, vaddr);
	if (!readmem(PADDR, (unsigned long long)pgda, &pgdv, sizeof(pgdv))) {
		ERRMSG("Can't read pgd\n");
		return NOT_PADDR;
	}

	puda = pud_offset(pgda, &pgdv, vaddr);
	if (!readmem(PADDR, (unsigned long long)puda, &pudv, sizeof(pudv))) {
		ERRMSG("Can't read pud\n");
		return NOT_PADDR;
	}

	pmda = pmd_offset(puda, &pudv, vaddr);
	if (!readmem(PADDR, (unsigned long long)pmda, &pmdv, sizeof(pmdv))) {
		ERRMSG("Can't read pmd\n");
		return NOT_PADDR;
	}

	switch (pmd_val(pmdv) & PMD_TYPE_MASK) {
	case PMD_TYPE_TABLE:
		ptea = pte_offset(&pmdv, vaddr);
		/* 64k page */
		if (!readmem(PADDR, (unsigned long long)ptea, &ptev, sizeof(ptev))) {
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

#endif /* __aarch64__ */
