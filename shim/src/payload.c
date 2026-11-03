/*
 * Payload loader - Extract and load XNU kernel from boot image
 *
 * Boot image format (Android boot.img):
 *   [Header]
 *   [Kernel - our shim]
 *   [Ramdisk - our payload containing XNU + ADT + firmware]
 *   [Second stage - unused]
 *   [DTB - original from ABL]
 */

#include "platform.h"
#include "uart.h"

/* Android boot image header (simplified) */
struct boot_img_hdr {
    uint8_t magic[8];           /* "ANDROID!" */
    uint32_t kernel_size;
    uint32_t kernel_addr;
    uint32_t ramdisk_size;
    uint32_t ramdisk_addr;
    uint32_t second_size;
    uint32_t second_addr;
    uint32_t tags_addr;
    uint32_t page_size;
    uint32_t unused[2];
    char name[16];
    char cmdline[512];
    uint32_t id[8];
    char extra_cmdline[1024];
} __attribute__((packed));

/* Payload format (custom) */
struct payload_hdr {
    uint32_t magic;             /* 'XPLD' */
    uint32_t version;           /* 1 */
    uint32_t kernel_offset;
    uint32_t kernel_size;
    uint32_t adt_offset;
    uint32_t adt_size;
    uint32_t bootargs_offset;
    uint32_t bootargs_size;
    uint32_t num_blobs;         /* Firmware blobs */
    uint32_t reserved[7];
} __attribute__((packed));

struct blob_entry {
    char name[32];              /* e.g. "GPU-PM4" */
    uint32_t offset;
    uint32_t size;
    uint32_t load_addr_lo;
    uint32_t load_addr_hi;
} __attribute__((packed));

#define PAYLOAD_MAGIC 0x444C5058  /* 'XPLD' */

/* Shim is loaded at this address by ABL */
#define SHIM_LOAD_BASE  0x80080000

/* Find our own boot image header */
static struct boot_img_hdr *find_boot_header(void) {
    /* ABL loads the entire boot.img to memory */
    /* We are running from kernel part, header is before us */

    uint8_t *search = (uint8_t *)(SHIM_LOAD_BASE - 0x1000);
    struct boot_img_hdr *hdr;

    /* Search backward for "ANDROID!" magic */
    for (int i = 0; i < 0x10000; i += 0x1000) {
        hdr = (struct boot_img_hdr *)(search - i);
        if (hdr->magic[0] == 'A' &&
            hdr->magic[1] == 'N' &&
            hdr->magic[2] == 'D' &&
            hdr->magic[3] == 'R' &&
            hdr->magic[4] == 'O' &&
            hdr->magic[5] == 'I' &&
            hdr->magic[6] == 'D' &&
            hdr->magic[7] == '!') {
            return hdr;
        }
    }

    return NULL;
}

/* Align to page size */
static inline uint32_t page_align(uint32_t size, uint32_t page_size) {
    return ((size + page_size - 1) / page_size) * page_size;
}

/* Parse Mach-O header to find entry point */
static uint64_t parse_macho_entry(void *kernel_base) {
    /* Mach-O magic: 0xFEEDFACF (64-bit) */
    uint32_t *magic = (uint32_t *)kernel_base;
    if (*magic != 0xFEEDFACF && *magic != 0xCFFAEDFE) {
        uart_puts("Error: Invalid Mach-O magic\n");
        return 0;
    }

    /* Simplified: XNU entry is usually at offset 0x1000 */
    /* Real implementation would parse LC_UNIXTHREAD load command */
    return (uint64_t)kernel_base + 0x1000;
}

/**
 * load_payload - Extract XNU kernel and ADT from ramdisk
 *
 * Returns: XNU entry point in x0, ADT pointer in x1
 */
uint64_t load_payload_impl(uint64_t *adt_ptr_out) {
    uart_puts("Loading XNU payload...\n");

    /* Find boot image header */
    struct boot_img_hdr *boot_hdr = find_boot_header();
    if (!boot_hdr) {
        platform_panic("Failed to find boot image header");
    }

    uart_puts("  Boot header found at 0x");
    uart_puthex((uint64_t)boot_hdr);
    uart_puts("\n");

    /* Calculate ramdisk location */
    uint32_t page_size = boot_hdr->page_size;
    if (page_size == 0) page_size = 2048;

    uint8_t *ramdisk_addr = (uint8_t *)boot_hdr;
    ramdisk_addr += page_size;  /* Skip header */
    ramdisk_addr += page_align(boot_hdr->kernel_size, page_size);  /* Skip shim */

    uart_puts("  Ramdisk at 0x");
    uart_puthex((uint64_t)ramdisk_addr);
    uart_puts(" (");
    uart_putdec(boot_hdr->ramdisk_size);
    uart_puts(" bytes)\n");

    /* Parse payload header */
    struct payload_hdr *payload = (struct payload_hdr *)ramdisk_addr;
    if (payload->magic != PAYLOAD_MAGIC) {
        uart_puts("  Expected: 0x");
        uart_puthex(PAYLOAD_MAGIC);
        uart_puts("\n");
        uart_puts("  Got: 0x");
        uart_puthex(payload->magic);
        uart_puts("\n");
        platform_panic("Invalid payload magic");
    }

    if (payload->version != 1) {
        platform_panic("Unsupported payload version");
    }

    uart_puts("  Payload version ");
    uart_putdec(payload->version);
    uart_puts("\n");

    /* Load XNU kernel */
    uart_puts("  Loading XNU kernel...\n");
    uart_puts("    Size: ");
    uart_putdec(payload->kernel_size / 1024);
    uart_puts(" KB\n");

    uint64_t kernel_load_addr = platform_get_kernel_load_addr();
    uint8_t *kernel_src = ramdisk_addr + payload->kernel_offset;
    uint8_t *kernel_dst = (uint8_t *)kernel_load_addr;

    /* Copy kernel */
    for (uint32_t i = 0; i < payload->kernel_size; i++) {
        kernel_dst[i] = kernel_src[i];
    }

    uart_puts("    Loaded to 0x");
    uart_puthex(kernel_load_addr);
    uart_puts("\n");

    /* Load ADT */
    uart_puts("  Loading Apple Device Tree...\n");
    uart_puts("    Size: ");
    uart_putdec(payload->adt_size / 1024);
    uart_puts(" KB\n");

    uint64_t adt_load_addr = kernel_load_addr + 0x01000000;  /* +16MB from kernel */
    uint8_t *adt_src = ramdisk_addr + payload->adt_offset;
    uint8_t *adt_dst = (uint8_t *)adt_load_addr;

    /* Copy ADT */
    for (uint32_t i = 0; i < payload->adt_size; i++) {
        adt_dst[i] = adt_src[i];
    }

    uart_puts("    Loaded to 0x");
    uart_puthex(adt_load_addr);
    uart_puts("\n");

    /* Load firmware blobs */
    if (payload->num_blobs > 0) {
        uart_puts("  Loading firmware blobs: ");
        uart_putdec(payload->num_blobs);
        uart_puts("\n");

        struct blob_entry *blobs = (struct blob_entry *)(
            ramdisk_addr + sizeof(struct payload_hdr)
        );

        for (uint32_t i = 0; i < payload->num_blobs; i++) {
            struct blob_entry *blob = &blobs[i];

            uart_puts("    ");
            uart_puts(blob->name);
            uart_puts(": ");
            uart_putdec(blob->size / 1024);
            uart_puts(" KB\n");

            /* Firmware is loaded to specific addresses or kept in place */
            /* XNU drivers will access them via ADT properties */
        }
    }

    /* Parse XNU entry point */
    uint64_t entry = parse_macho_entry((void *)kernel_load_addr);
    if (entry == 0) {
        platform_panic("Failed to parse XNU entry point");
    }

    uart_puts("  XNU entry point: 0x");
    uart_puthex(entry);
    uart_puts("\n");

    uart_puts("✓ Payload loaded successfully\n");
    uart_puts("\n");
    uart_puts("Booting XNU kernel...\n");
    uart_puts("\n");

    *adt_ptr_out = adt_load_addr;
    return entry;
}

/* Assembly wrapper expects two return values */
void load_payload(void);
__asm__(
    ".global load_payload\n"
    "load_payload:\n"
    "    stp x29, x30, [sp, #-16]!\n"
    "    mov x29, sp\n"
    "    sub sp, sp, #16\n"
    "    mov x0, sp\n"  /* Address to store ADT pointer */
    "    bl load_payload_impl\n"
    "    mov x1, x0\n"  /* Entry point */
    "    ldr x0, [sp]\n"  /* ADT pointer */
    "    mov sp, x29\n"
    "    ldp x29, x30, [sp], #16\n"
    "    ret\n"
);
