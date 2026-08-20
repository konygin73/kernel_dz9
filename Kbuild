DRV_NAME := kernel_msgpool
obj-m := $(DRV_NAME).o

$(DRV_NAME)-y := src/main.o \
                 src/alloc.o \
                 src/params.o \
                 src/queue.o

ccflags-y := -I$(src)/src
