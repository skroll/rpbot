# standard
sp             := $(sp).x
dirstack_$(sp) := $(d)
d              := $(dir)

STD_INC_$(d) := -I$(d)/../src/util -I$(d)/../src/ircsm
STD_LIB_$(d) := $(d)/../src/util/util.a $(d)/../src/ircsm/ircsm.a

OBJS_$(d) := $(d)/parse_test.o
TGTS_$(d) := $(d)/parse_test

DEPS_$(d) := $(OBJS_$(d):%=%.d)
CLEAN := $(CLEAN) $(OBJS_$(d)) $(DEPS_$(d)) $(d)/parse_test

$(OBJS_$(d)): CF_TGT := $(STD_INC_$(d))

$(d)/parse_test: LL_TGT := $(STD_LIB_$(d))
$(d)/parse_test: $(OBJS_$(d)) src/util/util.a src/ircsm/ircsm.a
	$(LINK)

TGT_TESTS := $(TGT_TESTS) $(TGTS_$(d))

# standard
-include $(DEPS_$(d))
d  := $(dirstack_$(sp))
sp := $(basename $(sp))

