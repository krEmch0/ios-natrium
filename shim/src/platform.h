/*
 * Platform header for Mi 5s Plus boot shim
 */

#ifndef _PLATFORM_H_
#define _PLATFORM_H_

#include <stdint.h>
#include <stddef.h>

/* Platform functions */
void platform_early_init(void *abl_dtb);
uint64_t platform_get_kernel_load_addr(void);
void platform_panic(const char *msg);

/* Payload loader */
uint64_t load_payload_impl(uint64_t *adt_ptr_out);

#endif /* _PLATFORM_H_ */
