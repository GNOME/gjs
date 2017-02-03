# NMake Makefile portion for compilation rules
# Items in here should not need to be edited unless
# one is maintaining the NMake build files.  The format
# of NMake Makefiles here are different from the GNU
# Makefiles.  Please see the comments about these formats.

# Inference rules for compiling the .obj files.
# Used for libs and programs with more than a single source file.
# Format is as follows
# (all dirs must have a trailing '\'):
#
# {$(srcdir)}.$(srcext){$(destdir)}.obj::
# 	$(CC)|$(CXX) $(cflags) /Fo$(destdir) /c @<<
# $<
# <<
{..\modules\}.cpp{$(CFG)\$(PLAT)\module-console\}.obj::
	$(CXX) $(CFLAGS) $(LIBGJS_CFLAGS) /Fo$(CFG)\$(PLAT)\module-console\ /c @<<
$<
<<

{..\modules\}.cpp{$(CFG)\$(PLAT)\module-system\}.obj::
	$(CXX) $(CFLAGS) $(LIBGJS_CFLAGS) /Fo$(CFG)\$(PLAT)\module-system\ /c @<<
$<
<<

{..\modules\}.cpp{$(CFG)\$(PLAT)\module-cairo\}.obj::
	$(CXX) $(CFLAGS) $(LIBGJS_CFLAGS) /Fo$(CFG)\$(PLAT)\module-cairo\ /c @<<
$<
<<

{..\gi\}.cpp{$(CFG)\$(PLAT)\libgjs\}.obj::
	$(CXX) $(CFLAGS) $(LIBGJS_CFLAGS) /Fo$(CFG)\$(PLAT)\libgjs\ /c @<<
$<
<<

{..\gjs\}.cpp{$(CFG)\$(PLAT)\libgjs\}.obj::
	$(CXX) $(CFLAGS) $(LIBGJS_CFLAGS) /Fo$(CFG)\$(PLAT)\libgjs\ /c @<<
$<
<<

{..\libgjs-private\}.cpp{$(CFG)\$(PLAT)\libgjs\}.obj::
	$(CXX) $(CFLAGS) $(LIBGJS_CFLAGS) /Fo$(CFG)\$(PLAT)\libgjs\ /c @<<
$<
<<

{..\libgjs-private\}.c{$(CFG)\$(PLAT)\libgjs\}.obj::
	$(CC) $(CFLAGS) $(LIBGJS_CFLAGS) /Fo$(CFG)\$(PLAT)\libgjs\ /c @<<
$<
<<

{..\modules\}.cpp{$(CFG)\$(PLAT)\libgjs\}.obj::
	$(CXX) $(CFLAGS) $(LIBGJS_CFLAGS) /Fo$(CFG)\$(PLAT)\libgjs\ /c @<<
$<
<<

{..\util\}.cpp{$(CFG)\$(PLAT)\libgjs\}.obj::
	$(CXX) $(CFLAGS) $(LIBGJS_CFLAGS) /Fo$(CFG)\$(PLAT)\libgjs\ /c @<<
$<
<<

{$(CFG)\$(PLAT)\module-resources\}.c{$(CFG)\$(PLAT)\libgjs\}.obj::
	$(CC) $(CFLAGS) $(LIBGJS_CFLAGS) /Fo$(CFG)\$(PLAT)\libgjs\ /c @<<
$<
<<

{..\gjs\}.cpp{$(CFG)\$(PLAT)\gjs-console\}.obj::
	$(CXX) $(CFLAGS) $(GJS_CFLAGS) /Fo$(CFG)\$(PLAT)\gjs-console\ /c @<<
$<
<<

# Rules for building .lib files
$(CFG)\$(PLAT)\gjs.lib: $(LIBGJS_DLL_FILENAME).dll

# Rules for linking DLLs
# Format is as follows (the mt command is needed for MSVC 2005/2008 builds):
# $(dll_name_with_path): $(dependent_libs_files_objects_and_items)
#	link /DLL [$(linker_flags)] [$(dependent_libs)] [/def:$(def_file_if_used)] [/implib:$(lib_name_if_needed)] -out:$@ @<<
# $(dependent_objects)
# <<
# 	@-if exist $@.manifest mt /manifest $@.manifest /outputresource:$@;2
$(LIBGJS_DLL_FILENAME).dll:		\
$(GJS_INCLUDED_MODULES)			\
$(CFG)\$(PLAT)\module-resources		\
$(CFG)\$(PLAT)\libgjs			\
$(module_resources_generated_srcs)	\
$(libgjs_dll_OBJS)
	link /DLL $(LDFLAGS) $(GJS_INCLUDED_MODULES)		\
	$(LIBGJS_DEP_LIBS) /implib:$(CFG)\$(PLAT)\gjs.lib	\
	-out:$@ @<<
$(libgjs_dll_OBJS)
<<
	@-if exist $@.manifest mt /manifest $@.manifest /outputresource:$@;2

$(CFG)\$(PLAT)\module-console.lib: ..\config.h $(CFG)\$(PLAT)\module-console $(module_console_OBJS)
	lib $(ARFLAGS) -out:$@ @<<
$(module_console_OBJS)
<<

$(CFG)\$(PLAT)\module-system.lib: ..\config.h $(CFG)\$(PLAT)\module-system $(module_system_OBJS)
	lib $(ARFLAGS) -out:$@ @<<
$(module_system_OBJS)
<<

$(CFG)\$(PLAT)\module-cairo.lib: ..\config.h $(CFG)\$(PLAT)\module-cairo $(module_cairo_OBJS)
	lib $(ARFLAGS) -out:$@ @<<
$(module_cairo_OBJS)
<<

# Rules for linking Executables
# Format is as follows (the mt command is needed for MSVC 2005/2008 builds):
# $(dll_name_with_path): $(dependent_libs_files_objects_and_items)
#	link [$(linker_flags)] [$(dependent_libs)] -out:$@ @<<
# $(dependent_objects)
# <<
# 	@-if exist $@.manifest mt /manifest $@.manifest /outputresource:$@;1

$(CFG)\$(PLAT)\gjs-console.exe: $(CFG)\$(PLAT)\gjs.lib $(CFG)\$(PLAT)\gjs-console $(gjs_OBJS)
	link $(LDFLAGS) $(CFG)\$(PLAT)\gjs.lib $(GJS_BASE_LIBS) -out:$@ $(gjs_OBJS)
	@-if exist $@.manifest mt /manifest $@.manifest /outputresource:$@;1

clean:
	@-if exist $(CFG)\$(PLAT)\GjsPrivate-1.0.typelib del /f /q $(CFG)\$(PLAT)\GjsPrivate-1.0.typelib
	@-if exist $(CFG)\$(PLAT)\GjsPrivate-1.0.gir del /f /q $(CFG)\$(PLAT)\GjsPrivate-1.0.gir
	@-if exist $(CFG)\$(PLAT)\gjs_private_list del /f /q $(CFG)\$(PLAT)\gjs_private_list
	@-del /f /q $(CFG)\$(PLAT)\*.pdb
	@-if exist $(CFG)\$(PLAT)\gjs-console.exe.manifest del /f /q $(CFG)\$(PLAT)\gjs-console.exe.manifest
	@-if exist $(CFG)\$(PLAT)\gjs-console.exe del /f /q $(CFG)\$(PLAT)\gjs-console.exe
	@-del /f /q $(CFG)\$(PLAT)\*.dll.manifest
	@-del /f /q $(CFG)\$(PLAT)\*.dll
	@-del /f /q $(CFG)\$(PLAT)\*.ilk
	@-del /f /q $(CFG)\$(PLAT)\*.exp
	@-del /f /q $(CFG)\$(PLAT)\*.lib
	@-if exist $(CFG)\$(PLAT)\module-cairo.lib del /f /q $(CFG)\$(PLAT)\module-cairo\*.obj
	@-del /f /q $(CFG)\$(PLAT)\module-system\*.obj
	@-del /f /q $(CFG)\$(PLAT)\module-console\*.obj
	@-del /f /q $(CFG)\$(PLAT)\libgjs\*.obj
	@-del /f /q $(CFG)\$(PLAT)\gjs-console\*.obj
	@-del /f /q $(module_resources_generated_srcs)
	@-del vc$(VSVER)0.pdb
	@-del ..\config.h
