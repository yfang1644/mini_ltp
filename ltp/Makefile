OPT = /opt
HEAP_PTRS = 0
PLATFORMS = i86-redhat
# sparc-sol9
# i86-freebsd

all:
		for PF in $(PLATFORMS); \
			do \
				cd $$PF; \
				mkdir -p bin; \
				mkdir -p lib; \
				make all ROOT=$(OPT) PTRS=$(HEAP_PTRS); \
				cd ..; \
			done

clean:
		cd doc; \
		make -i clean; \
		cd ..; \
		for PF in $(PLATFORMS); \
			do cd $$PF; make -i clean; cd ..; done

install:
		cd doc; \
		make -i install ROOT=$(OPT); \
		cd ..; \
		for PF in $(PLATFORMS); \
			do cd $$PF; make -i install ROOT=$(OPT); cd ..; done
