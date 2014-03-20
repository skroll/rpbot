# standard
sp             := $(sp).x
dirstack_$(sp) := $(d)
d              := $(dir)

# subdirectories
dir := $(d)/util
include $(dir)/rules.mk

dir := $(d)/ircsm
include $(dir)/rules.mk

OBJS_$(d) := $(d)/rp_config.o \
             $(d)/rp_event.o \
             $(d)/rp_irc.o \
             $(d)/rp_options.o \
             $(d)/rpbot.o

DEPS_$(d) := $(OBJS_$(d):%=%.d)
CLEAN := $(CLEAN) $(OBJS_$(d)) $(DEPS_$(d)) $(d)/rpbot

TGTS_$(d) := $(d)/rpbot
TGT_BIN := $(TGT_BIN) $(TGTS_$(d))

$(OBJS_$(d)): CF_TGT := -I$(d) -I$(d)/util -I$(d)/ircsm

$(d)/rpbot: LL_TGT := -lyajl -lanl $(d)/util/util.a $(d)/ircsm/ircsm.a
$(d)/rpbot: $(OBJS_$(d)) $(d)/util/util.a $(d)/ircsm/ircsm.a
	$(LINK)

# standard
-include $(DEPS_$(d))
d  := $(dirstack_$(sp))
sp := $(basename $(sp))

