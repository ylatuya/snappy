AUTOMAKE_OPTIONS = 1.7

SUBDIRS = 		\
	src data

DIST_SUBDIRS = $(SUBDIRS)

EXTRA_DIST = \
	ChangeLog autogen.sh \
	AUTHORS COPYING NEWS README ToDo

pkgconfigdir = $(libdir)pkgconfig

ACLOCAL_AMFLAGS = 

DISTCLEANFILES = 

all-local: 

dist-hook:
	@if test -d "$(srcdir)/.git"; \
	then \
		echo Creating ChangeLog && \
		( cd "$(top_srcdir)" && \
		  echo '# Generated by Makefile. Do not edit.'; echo; \
		  git log --stat ) > ChangeLog.tmp \
		&& mv -f ChangeLog.tmp $(distdir)/ChangeLog \
		|| ( rm -f ChangeLog.tmp ; \
		     echo Failed to generate ChangeLog >&2 ); \
	else \
		echo A git clone is required to generate a ChangeLog >&2; \
	fi
