sp             := $(sp).x
dirstack_$(sp) := $(d)
d              := $(dir)

OBJS_$(d) := $(d)/util/rp_fifo.o \
             $(d)/util/rp_os.o \
             $(d)/util/rp_palloc.o \
             $(d)/util/rp_slab.o \
             $(d)/rp_config.o \
             $(d)/rp_event.o \
             $(d)/rp_irc.o \
             $(d)/rp_irc_sm.o \
             $(d)/rp_options.o \
             $(d)/rpbot.o

DEPS_$(d) := $(OBJS_$(d):%=%.d)
CLEAN := $(CLEAN) $(OBJS_$(d)) $(DEPS_$(d)) $(d)/rp_irc_sm.c $(d)/rpbot

TGTS_$(d) := $(d)/rpbot
TGT_BIN := $(TGT_BIN) $(TGTS_$(d))

$(OBJS_$(d)): CF_TGT := -I$(d) -I$(d)/util

$(d)/rp_irc_sm.c: $(d)/rp_irc_sm.rl

$(d)/rpbot: LL_TGT := -lyajl -lanl
$(d)/rpbot: $(OBJS_$(d))
	$(LINK)

-include $(DEPS_$(d))
d  := $(dirstack_$(sp))
sp := $(basename $(sp))

