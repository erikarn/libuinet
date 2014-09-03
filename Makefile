
SUBDIRS=lib bin

config all clean install:
	for d in $(SUBDIRS); do ( cd $$d && $(MAKE) $@) ||  exit 1 ; done
