sp             := $(sp).x
dirstack_$(sp) := $(d)
d              := $(dir)

OBJS_$(d) := $(d)/rp_fifo.o \
             $(d)/rp_os.o \
             $(d)/rp_palloc.o \
             $(d)/rp_slab.o \
             $(d)/rp_string.o

DEPS_$(d) := $(OBJS_$(d):%=%.d)
CLEAN := $(CLEAN) $(OBJS_$(d)) $(DEPS_$(d)) $(d)/util.a

$(OBJS_$(d)): CF_TGT := -I$(d)

$(d)/util.a: $(OBJS_$(d))
	$(ARCHIVE)

-include $(DEPS_$(d))
d  := $(dirstack_$(sp))
sp := $(basename $(sp))

