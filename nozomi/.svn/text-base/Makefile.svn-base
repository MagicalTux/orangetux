#
# Makefile for the Linux Nozomi (GlobeTrotter PC Cards)
#
# Original makefile by Peter Johanson
#
# NOTE: This make file can serve as both an external Makefile (launched
#       directly by the user), or as the sub-dir Makefile used by the kernel
# 	build system.

# If CONFIG_NOZOMI isn't set, we'll assume the user has never configured
# their kernel to include this module and set up some defaults.
#
ifndef CONFIG_NOZOMI
EXTERNAL_BUILD=y
CONFIG_NOZOMI=m
CONFIG_NOZOMI_DEBUG=y

# NB: include here any default config as CONFIG_NOZOMI_*
endif


ifneq ($(M),)
	DIR=$(M)
else
	DIR=$(shell pwd)
endif

ifeq ($(CONFIG_NOZOMI_DEBUG),y)
	EXTRA_CFLAGS += -g -Wa,-adhlms=$@.lst
endif

list-m :=
list-$(CONFIG_NOZOMI) += nozomi
obj-$(CONFIG_NOZOMI) += nozomi.o

#
# Begin dual Makefile mode here.  First we provide support for when we
# are being invoked by the kernel build system
#
ifneq ($(KERNELRELEASE),)

#EXTRA_CFLAGS += -DCONFIG_PM

ifeq ($(EXTERNAL_BUILD),y)
ifeq ($(CONFIG_NOZOMI_DEBUG),y)
EXTRA_CFLAGS += -DCONFIG_NOZOMI_DEBUG
endif
endif

else 
# Here we begin the portion that is executed if the user invoked this Makefile
# directly.

# KSRC should be set to the path to your sources
# modules are installed into KMISC
KVER  := $(shell uname -r)
KSRC := /lib/modules/$(KVER)/build
KMISC := /lib/modules/$(KVER)/kernel/drivers/char/
KMISC_INC := /lib/modules/$(KVER)/include

# KSRC_OUTPUT should be overridden if you are using a 2.6 kernel that
# has it's output sent elsewhere via KBUILD_OUTPUT= or O=
KSRC_OUTPUT := $(KSRC)

VERFILE := $(KSRC_OUTPUT)/include/linux/version.h
KERNELRELEASE := $(shell \
	if [ -r $(VERFILE) ]; then \
		(cat $(VERFILE); echo UTS_RELEASE) | \
		$(CC) -I$(KSRC_OUTPUT) $(CFLAGS) -E - | \
		tail -n 1 | \
		xargs echo; \
        else \
		uname -r; \
	fi)

MODPATH := $(DESTDIR)/lib/modules/$(KERNELRELEASE)

all: modules

clean:
	rm -f *.{mod.c,mod,o,ko,rej,orig} .*.{stamp,flags,cmd} .lst *.lst *~ 
	rm -rf $(DIR)/tmp $(DIR)/.tmp_versions Module.symvers Modules.symvers
	for file in *.{c,h}; do \
		sed -i -e "s:\ *$$::g" -e "s:\t*$$::g" $$file; \
	done

distclean: clean
	rm -f tags TAGS

MODVERDIR=$(DIR)/tmp/.tmp_versions
modules:
	mkdir -p $(MODVERDIR)
	$(MAKE) -C $(KSRC) M=$(shell pwd) MODVERDIR=$(MODVERDIR) modules

install:
	install -d $(KMISC)
	install -m 644 -c $(addsuffix .ko,$(list-m)) $(KMISC)
	/sbin/depmod -a ${KVER}
	@echo "NOTE:  This driver is for development and validation purposes only "
	@echo "and has not been tested for regulatory compliance.  By using this "
	@echo "driver you assume responsibility for any compliance issues that may "
	@echo "arise."

uninstall:
	rm -rf $(addprefix $(KMISC),$(addsuffix .ko,$(list-m)))
	/sbin/depmod -a ${KVER}

endif # End of internal build


.PHONY: check_inc

