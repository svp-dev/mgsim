#
# MicroGrid Simulator, version 1.00
# Copyright Mike Lankamp 2007
#
# Makefile for Linux.
#
all: release debug
release: MGSim
debug: MGSim.dbg

CC               = g++
CPPFLAGS_COMMON  = -Wall
CPPFLAGS_RELEASE = $(CPPFLAGS_COMMON) -O2 -DNDEBUG
CPPFLAGS_DEBUG   = $(CPPFLAGS_COMMON) -O0 -g

OBJDIR   = objs
DEPDIR   = deps
LDFLAGS  = -lreadline -lncurses
OBJS     = $(patsubst %.cpp,$(OBJDIR)/%.o,$(wildcard *.cpp))
DEPS     = $(patsubst %.cpp,$(DEPDIR)/%.d,$(wildcard *.cpp))

.PHONY: clean tidy all debug release
.SUFFIXES:
.SILENT:

MGSim.dbg: $(subst .o,.dbg.o,$(OBJS))
	echo LINK $@
	$(CC) $(LDFLAGS) -o MGSim.dbg $^

MGSim: $(OBJS)
	echo LINK $@
	$(CC) $(LDFLAGS) -o MGSim $^

-include $(DEPDIR)/*.d
Makefile: $(DEPS)

$(OBJDIR)/%.dbg.o: %.cpp
	echo CC $*.o
	if [ ! -e $(OBJDIR) ]; then mkdir $(OBJDIR); fi
	$(CC) -c $(CPPFLAGS_DEBUG) -o $@ $<

$(OBJDIR)/%.o: %.cpp
	echo CC $*.o
	if [ ! -e $(OBJDIR) ]; then mkdir $(OBJDIR); fi
	$(CC) -c $(CPPFLAGS_RELEASE) -o $@ $<

$(DEPDIR)/%.d: %.cpp
	if [ ! -e $(DEPDIR) ]; then mkdir $(DEPDIR); fi
	$(CC) -MM $< | sed 's,\($*\)\.o[ :]*,$(OBJDIR)/\1.o $@ : ,g' > $@

tidy:
	@rm -rf $(OBJDIR)

clean: tidy
	@rm -rf MGSim $(DEPDIR)
