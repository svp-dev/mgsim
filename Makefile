#
# MicroGrid Simulator, version 1.00
# Copyright Mike Lankamp 2007
#
# Makefile for Linux.
#
all: MGSim

CC       = g++
CPPFLAGS = -O2 -Wall -DNDEBUG
OBJDIR   = objs
DEPDIR   = deps
LDFLAGS  = -lreadline -lncurses
OBJS     = $(patsubst %.cpp,$(OBJDIR)/%.o,$(wildcard *.cpp))
DEPS     = $(patsubst %.cpp,$(DEPDIR)/%.d,$(wildcard *.cpp))

.PHONY: clean tidy
.SUFFIXES:
.SILENT:

MGSim: $(OBJS)
	echo LINK $@
	$(CC) $(LDFLAGS) -o MGSim $(OBJS)

-include $(DEPDIR)/*.d
Makefile: $(DEPS)

$(OBJDIR)/%.o: %.cpp
	echo CC $*.o
	if [ ! -e $(OBJDIR) ]; then mkdir $(OBJDIR); fi
	$(CC) -c $(CPPFLAGS) -o $@ $<

$(DEPDIR)/%.d: %.cpp
	if [ ! -e $(DEPDIR) ]; then mkdir $(DEPDIR); fi
	$(CC) -MM $< | sed 's,\($*\)\.o[ :]*,$(OBJDIR)/\1.o $@ : ,g' > $@

tidy:
	@rm -rf $(OBJDIR)

clean: tidy
	@rm -rf MGSim $(DEPDIR)
