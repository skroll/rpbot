.SUFFIXES:
.SUFFIXES: .c .o

all: targets

dir := src
include $(dir)/rules.mk

%.o: %.c
	$(COMP)

%.c: %.rl
	$(RAGEL)

%: %.o
	$(LINK)

%: %.c
	$(COMPLINK)

.PHONY: targets
targets: $(TGT_BIN) $(TGT_LIB)

.PHONY: clean
clean:
	rm -f $(CLEAN)

.SECONDARY: $(CLEAN)

