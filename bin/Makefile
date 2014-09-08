
SUBDIRS=echo echo++ passive sysctl tproxy vmstat

config all clean install maintainer-clean:
	for d in $(SUBDIRS); do ( cd $$d && $(MAKE) $@) ||  exit 1 ; done
