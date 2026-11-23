# Standalone NEWOS application Makefile (copy this directory to start).
#
# NEWOS_SDK must point at the `sdk` directory of a NEWOS checkout. It defaults
# to the relative path from this template, so adjust it if you copy elsewhere:
#   make NEWOS_SDK=/home/me/newos/sdk
#   export NEWOS_SDK=/home/me/newos/sdk

NAME      ?= myapp
VERSION   ?= 1.0.0
NEWOS_SDK ?= $(abspath ../..)
NEWOS_CC  ?= $(NEWOS_SDK)/bin/newos-cc
NEWOS_PKG ?= $(NEWOS_SDK)/bin/newos-package

BUILD := build
SRCS  := $(wildcard *.c)
OUT   := $(BUILD)/$(NAME).elf

.PHONY: all package clean
all: $(OUT)

$(OUT): $(SRCS) $(NEWOS_CC)
	@mkdir -p $(dir $@)
	$(NEWOS_CC) -o $@ $(SRCS)

package: $(OUT)
	$(NEWOS_PKG) $(OUT) --name $(NAME) --version $(VERSION) \
	    --desc "$(NAME) for NEWOS" \
	    --out $(BUILD)/$(NAME)-$(VERSION)-x86_64.new

clean:
	rm -rf $(BUILD)
