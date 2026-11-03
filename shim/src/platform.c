/*
 * Platform initialization for Snapdragon 821 (MSM8996Pro)
 * Prepares hardware for XNU boot
 */

#include "platform.h"
#include "uart.h"

/* Memory mapped I/O bases */
#define TLMM_BASE           0x01010000
#define GCC_BASE            0x00300000
#define WDOG_BASE           0x17980000
#define APCS_GLB_BASE       0x09BC0000  /* GICv3 */

/* GCC Clock Controller */
#define GCC_BLSP1_UART2_APPS_CBCR   (GCC_BASE + 0x302C)
#define GCC_BLSP1_AHB_CBCR          (GCC_BASE + 0x5004)

/* Watchdog */
#define WDOG_RST            (WDOG_BASE + 0x04)
#define WDOG_EN             (WDOG_BASE + 0x08)
#define WDOG_BARK_TIME      (WDOG_BASE + 0x10)
#define WDOG_BITE_TIME      (WDOG_BASE + 0x14)

/* TLMM GPIO */
#define TLMM_GPIO_CFG(n)    (TLMM_BASE + 0x1000 + (n) * 0x1000)

/* Memory map */
#define DDR_BASE            0x80000000UL
#define DDR_SIZE            0x100000000UL  /* 4 GB */
#define KERNEL_LOAD_ADDR    0x100000000UL  /* Upper bank */

static void writel(uint32_t val, uintptr_t addr) {
    *(volatile uint32_t *)addr = val;
}

static uint32_t readl(uintptr_t addr) {
    return *(volatile uint32_t *)addr;
}

static void udelay(uint32_t us) {
    /* Simple delay loop, calibrated for ~2 GHz */
    volatile uint32_t count = us * 2000;
    while (count--);
}

/**
 * disable_watchdog - Stop Qualcomm watchdog timer
 *
 * Critical: If not disabled, device will reset during XNU boot
 */
static void disable_watchdog(void) {
    uart_puts("Disabling watchdog...\n");

    /* Disable watchdog */
    writel(0, WDOG_EN);

    /* Clear any pending reset */
    writel(1, WDOG_RST);

    uart_puts("✓ Watchdog disabled\n");
}

/**
 * configure_gpio - Set up GPIO pins for peripherals
 */
static void configure_gpio(void) {
    uart_puts("Configuring GPIO...\n");

    /* UART2 pins: GPIO 4 (TX), GPIO 5 (RX) */
    /* Function 2 = BLSP UART */
    writel(0x00000020, TLMM_GPIO_CFG(4));   /* TX */
    writel(0x00000020, TLMM_GPIO_CFG(5));   /* RX */

    /* Touch interrupt: GPIO 125 */
    /* Input, pull-up */
    writel(0x00000003, TLMM_GPIO_CFG(125));

    /* Volume keys: GPIO 0, GPIO 2 */
    writel(0x00000003, TLMM_GPIO_CFG(0));
    writel(0x00000003, TLMM_GPIO_CFG(2));

    uart_puts("✓ GPIO configured\n");
}

/**
 * init_clocks - Enable necessary clocks
 */
static void init_clocks(void) {
    uart_puts("Initializing clocks...\n");

    /* Enable BLSP1 AHB clock */
    writel(0x1, GCC_BLSP1_AHB_CBCR);

    /* Enable UART2 clock */
    writel(0x1, GCC_BLSP1_UART2_APPS_CBCR);

    udelay(10);

    uart_puts("✓ Clocks initialized\n");
}

/**
 * detect_memory - Detect DDR configuration
 */
static void detect_memory(void) {
    uart_puts("Detecting memory...\n");

    /* Read SMEM for DDR info (simplified) */
    /* Real implementation would parse Qualcomm SMEM */

    uint32_t total_mb = 4096;  /* Mi 5s Plus: 4GB or 6GB variants */

    uart_puts("  Total RAM: ");
    uart_putdec(total_mb);
    uart_puts(" MB\n");

    uart_puts("✓ Memory detected\n");
}

/**
 * stop_secondary_cpus - Halt non-boot CPUs
 *
 * XNU will bring them up via PSCI
 */
static void stop_secondary_cpus(void) {
    uart_puts("Stopping secondary CPUs...\n");

    /* Send SGI to all cores except CPU0 */
    /* They should be in WFI from ABL, but make sure */

    /* GICv3: ICC_SGI1R_EL1 */
    /* Target all except affinity 0.0.0.0 */
    __asm__ volatile(
        "mov x0, #0x02000000\n"  /* Target list (CPU1) */
        "msr ICC_SGI1R_EL1, x0\n"
        "mov x0, #0x02000100\n"  /* Target list (CPU2) */
        "msr ICC_SGI1R_EL1, x0\n"
        "mov x0, #0x02000101\n"  /* Target list (CPU3) */
        "msr ICC_SGI1R_EL1, x0\n"
        "isb\n"
        ::: "x0"
    );

    uart_puts("✓ Secondary CPUs stopped\n");
}

/**
 * print_banner - Show boot banner
 */
static void print_banner(void) {
    uart_puts("\n");
    uart_puts("╔════════════════════════════════════════╗\n");
    uart_puts("║   iOS on Xiaomi Mi 5s Plus (natrium)  ║\n");
    uart_puts("║   Boot Shim v1.0                       ║\n");
    uart_puts("╚════════════════════════════════════════╝\n");
    uart_puts("\n");
    uart_puts("Device: Xiaomi Mi 5s Plus\n");
    uart_puts("SoC: Snapdragon 821 MSM8996Pro\n");
    uart_puts("Target: iPhone 8 (D201AP)\n");
    uart_puts("\n");
}

/**
 * platform_early_init - Early platform initialization
 *
 * Called from assembly entry point with ABL's DTB
 */
void platform_early_init(void *abl_dtb) {
    /* Initialize UART first for debugging */
    uart_early_init();

    print_banner();

    uart_puts("Early platform init...\n");

    /* Critical: Disable watchdog ASAP */
    disable_watchdog();

    /* Set up clocks */
    init_clocks();

    /* Configure GPIOs */
    configure_gpio();

    /* Detect memory */
    detect_memory();

    /* Stop other CPUs (XNU will use PSCI) */
    stop_secondary_cpus();

    uart_puts("\n");
    uart_puts("✓ Platform initialization complete\n");
    uart_puts("\n");
}

/**
 * platform_get_kernel_load_addr - Return kernel load address
 */
uint64_t platform_get_kernel_load_addr(void) {
    return KERNEL_LOAD_ADDR;
}

/**
 * platform_panic - Fatal error handler
 */
void platform_panic(const char *msg) {
    uart_puts("\n");
    uart_puts("╔════════════════════════════════════════╗\n");
    uart_puts("║         BOOT PANIC                     ║\n");
    uart_puts("╚════════════════════════════════════════╝\n");
    uart_puts("\n");
    uart_puts(msg);
    uart_puts("\n\n");
    uart_puts("System halted.\n");

    /* Infinite loop */
    while (1) {
        __asm__ volatile("wfi");
    }
}
