#include <drivers/acpi.h>
#include <core/core_printk.h>
#include <lib/kernel/iru_string.h>
#include <mm/mm_heap.h>

int acpi_init(void) {
    printk("ACPI: initializing\n");
    return 0;
}

void acpi_dump(void) {
    printk("ACPI: dump not implemented\n");
}

struct acpi_madt *acpi_get_madt(void) { return NULL; }
struct acpi_fadt *acpi_get_fadt(void) { return NULL; }

bool acpi_parse(void) {
    printk("ACPI: parsing not yet implemented\n");
    return false;
}
