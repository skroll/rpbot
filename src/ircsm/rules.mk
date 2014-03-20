sp             := $(sp).x
dirstack_$(sp) := $(d)
d              := $(dir)

OBJS_$(d) := $(d)/rp_ircsm.o

$(d)/rp_ircsm.c: $(d)/rp_ircsm.rl

DEPS_$(d) := $(OBJS_$(d):%=%.d)
CLEAN := $(CLEAN) $(OBJS_$(d)) $(DEPS_$(d)) $(d)/ircsm.a

$(OBJS_$(d)): CF_TGT := -I$(d) -I$(d)/../util

$(d)/ircsm.a: $(OBJS_$(d))
	$(ARCHIVE)

-include $(DEPS_$(d))
d  := $(dirstack_$(sp))
sp := $(basename $(sp))

