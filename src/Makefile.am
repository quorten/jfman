## Process this file with automake to produce Makefile.in

INCLUDES = \
	-DPACKAGE_PREFIX=\""$(prefix)"\" \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	@PACKAGE_CFLAGS@

bin_PROGRAMS = jfman

jfman_SOURCES = \
	main.c \
	support.c \
	support.h \
	xmalloc.c \
	xmalloc.h

noinst_LIBRARIES = libcmdline.a

libcmdline_a_SOURCES = \
	cmdline.c \
	cmdline.h \
	xmalloc.h \
	shenv.c \
	shenv.h \
	shglob.h \
	noglob.c

jfman_LDADD = libcmdline.a @PACKAGE_LIBS@ $(INTLLIBS)

if WITH_WIN32

%-rc.o: %.rc
	$(RC) $(DEFS) $(DEFAULT_INCLUDES) -O coff -o $@ $<

## sndstud-rc.o: sndstud.rc ../svgs/sndstud.ico

## sndstud_SOURCES += sndstud.rc
## sndstud_LDADD += sndstud-rc.o
endif
