gjs_public_headers =		\
	gjs/context.h		\
	gjs/coverage.h		\
	gjs/gjs.h		\
	gjs/macros.h		\
	gjs/mem.h		\
	gjs/profiler.h		\
	util/error.h		\
	$(NULL)

# For historical reasons, some files live in gi/
# Some headers in the following list were formerly
# public

gjs_srcs =				\
	gi/arg.cpp			\
	gi/arg.h			\
	gi/boxed.cpp			\
	gi/boxed.h			\
	gi/closure.cpp			\
	gi/closure.h			\
	gi/enumeration.cpp		\
	gi/enumeration.h		\
	gi/foreign.cpp			\
	gi/foreign.h			\
	gi/fundamental.cpp		\
	gi/fundamental.h		\
	gi/function.cpp			\
	gi/function.h			\
	gi/gerror.cpp			\
	gi/gerror.h			\
	gi/gjs_gi_trace.h		\
	gi/gobject.cpp			\
	gi/gobject.h			\
	gi/gtype.cpp			\
	gi/gtype.h			\
	gi/interface.cpp		\
	gi/interface.h			\
	gi/ns.cpp			\
	gi/ns.h	        		\
	gi/object.cpp			\
	gi/object.h			\
	gi/param.cpp			\
	gi/param.h			\
	gi/private.cpp			\
	gi/private.h			\
	gi/repo.cpp			\
	gi/repo.h			\
	gi/toggle.cpp			\
	gi/toggle.h			\
	gi/union.cpp			\
	gi/union.h			\
	gi/value.cpp			\
	gi/value.h			\
	gi/wrapperutils.cpp		\
	gi/wrapperutils.h		\
	gjs/atoms.cpp			\
	gjs/atoms.h			\
	gjs/byteArray.cpp		\
	gjs/byteArray.h			\
	gjs/context.cpp			\
	gjs/context-private.h		\
	gjs/coverage.cpp 		\
	gjs/debugger.cpp		\
	gjs/deprecation.cpp		\
	gjs/deprecation.h		\
	gjs/engine.cpp			\
	gjs/engine.h			\
	gjs/global.cpp			\
	gjs/global.h			\
	gjs/importer.cpp		\
	gjs/importer.h			\
	gjs/jsapi-class.h		\
	gjs/jsapi-dynamic-class.cpp	\
	gjs/jsapi-util.cpp		\
	gjs/jsapi-util.h		\
	gjs/jsapi-util-args.h		\
	gjs/jsapi-util-error.cpp	\
	gjs/jsapi-util-root.h		\
	gjs/jsapi-util-string.cpp	\
	gjs/jsapi-wrapper.h		\
	gjs/mem.cpp			\
	gjs/mem-private.h		\
	gjs/module.h			\
	gjs/module.cpp			\
	gjs/native.cpp			\
	gjs/native.h			\
	gjs/profiler.cpp		\
	gjs/profiler-private.h		\
	gjs/stack.cpp			\
	modules/modules.cpp		\
	modules/modules.h		\
	util/error.cpp			\
	util/log.cpp			\
	util/log.h			\
	util/misc.cpp			\
	util/misc.h			\
	$(NULL)

# These files were part of a separate library
gjs_private_srcs =				\
	libgjs-private/gjs-gdbus-wrapper.c	\
	libgjs-private/gjs-gdbus-wrapper.h	\
	libgjs-private/gjs-util.c		\
	libgjs-private/gjs-util.h		\
	libgjs-private/gjs-gtk-util.h		\
	$(NULL)

gjs_gtk_private_srcs =			\
	libgjs-private/gjs-gtk-util.c	\
	$(NULL)

gjs_console_srcs =	\
	gjs/console.cpp	\
	$(NULL)
