#ifndef ACPI_H
#define ACPI_H

#include <core/core_types.h>

struct acpi_rsdp;
struct acpi_xsdt;
struct acpi_madt;
struct acpi_fadt;

int acpi_init(void);
void acpi_dump(void);
struct acpi_madt *acpi_get_madt(void);
struct acpi_fadt *acpi_get_fadt(void);
bool acpi_parse(void);

#endif
