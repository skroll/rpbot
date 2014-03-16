CF_ALL = -g -std=c99 -pedantic -Wall -Wvariadic-macros -Os -D_GNU_SOURCE -DRPBOT_PTR_SIZE=8
LF_ALL = -g
LL_ALL =

CC       = cc
INST     = install
AR       = ar
RL       = ragel
COMP     = $(CC) $(CF_ALL) $(CF_TGT) -o $@ -c $<
LINK     = $(CC) $(LF_ALL) $(LF_TGT) -o $@ $^ $(LL_TGT) $(LL_ALL)
COMPLINK = $(CC) $(CF_ALL) $(CF_TGT) $(LF_ALL) $(LF_TGT) -o $@ $< $(LL_TGT) $(LL_ALL)
ARCHIVE  = $(AR) rcs $@ $^
RAGEL    = $(RL) -o $@ $<

include rules.mk

