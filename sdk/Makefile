# NEWOS SDK examples.
#
# Build the generated sysroot (headers + libc.a + libnsh.a + app.ld) and link
# every example into sdk/build/<name>.elf with bin/newos-cc. This Makefile
# lives in the superproject, so it does not require a submodule commit.
#
#   make -C sdk            build all examples
#   make -C sdk check      build + sanity-check the ELF headers
#   make -C sdk clean      remove sdk/build
#
# Override NEWOS_CC / NEWOS_SYSROOT / CROSS_PREFIX as for bin/newos-cc.

ROOT       := ..
SYSROOT    := $(ROOT)/sysroot
NEWOS_CC   := $(ROOT)/sdk/bin/newos-cc
BUILD      := build

EXAMPLES   := 01-hello 02-args 03-files 04-sysinfo 05-framebuffer 06-processes \
              07-newpaint
IMAGES     := $(addprefix $(BUILD)/,$(addsuffix .elf,$(EXAMPLES)))

.PHONY: all sdk check clean
all: $(IMAGES)

# Only the pieces the host SDK needs. In particular this avoids the in-tree
# nasm-built start.o, which sdk/crt0/crt0.S replaces for external programs.
sdk:
	@$(MAKE) -C $(ROOT) \
		sysroot/usr/include/.newos-headers \
		sysroot/usr/lib/libc.a \
		sysroot/usr/lib/libnsh.a \
		sysroot/usr/lib/app.ld

define EXAMPLE_RULE
$(BUILD)/$(1).elf: $$(wildcard examples/$(1)/*.c) | sdk Makefile $(NEWOS_CC)
	@mkdir -p $$(dir $$@)
	@printf '  SDKCC %s\n' '$$@'
	@$(NEWOS_CC) -o $$@ $$^
endef
$(foreach e,$(EXAMPLES),$(eval $(call EXAMPLE_RULE,$(e))))

check: all
	@for f in $(IMAGES); do \
		readelf -h "$$f" | grep -q EXEC || { echo "bad ELF: $$f" >&2; exit 1; }; \
	done
	@echo "SDK examples: $(words $(EXAMPLES)) static ELF64 images OK"

clean:
	@rm -rf $(BUILD)
