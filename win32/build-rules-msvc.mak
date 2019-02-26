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
{..\modules\}.cpp{vs$(VSVER)\$(CFG)\$(PLAT)\module-console\}.obj::
	$(CXX) $(CFLAGS) $(LIBGJS_CFLAGS) /Fovs$(VSVER)\$(CFG)\$(PLAT)\module-console\ /Fdvs$(VSVER)\$(CFG)\$(PLAT)\module-console\ /c @<<
$<
<<

{..\modules\}.cpp{vs$(VSVER)\$(CFG)\$(PLAT)\module-system\}.obj::
	$(CXX) $(CFLAGS) $(LIBGJS_CFLAGS) /Fovs$(VSVER)\$(CFG)\$(PLAT)\module-system\ /Fdvs$(VSVER)\$(CFG)\$(PLAT)\module-system\ /c @<<
$<
<<

{..\modules\}.cpp{vs$(VSVER)\$(CFG)\$(PLAT)\module-cairo\}.obj::
	$(CXX) $(CFLAGS) $(LIBGJS_CFLAGS) /Fovs$(VSVER)\$(CFG)\$(PLAT)\module-cairo\ /Fdvs$(VSVER)\$(CFG)\$(PLAT)\module-cairo\ /c @<<
$<
<<

{..\gi\}.cpp{vs$(VSVER)\$(CFG)\$(PLAT)\libgjs\}.obj::
	$(CXX) $(CFLAGS) $(LIBGJS_CFLAGS) /Fovs$(VSVER)\$(CFG)\$(PLAT)\libgjs\ /Fdvs$(VSVER)\$(CFG)\$(PLAT)\libgjs\ /c @<<
$<
<<

{..\gjs\}.cpp{vs$(VSVER)\$(CFG)\$(PLAT)\libgjs\}.obj::
	$(CXX) $(CFLAGS) $(LIBGJS_CFLAGS) /Fovs$(VSVER)\$(CFG)\$(PLAT)\libgjs\ /Fdvs$(VSVER)\$(CFG)\$(PLAT)\libgjs\ /c @<<
$<
<<

{..\libgjs-private\}.cpp{vs$(VSVER)\$(CFG)\$(PLAT)\libgjs\}.obj::
	$(CXX) $(CFLAGS) $(LIBGJS_CFLAGS) /Fovs$(VSVER)\$(CFG)\$(PLAT)\libgjs\ /Fdvs$(VSVER)\$(CFG)\$(PLAT)\libgjs\ /c @<<
$<
<<

{..\libgjs-private\}.c{vs$(VSVER)\$(CFG)\$(PLAT)\libgjs\}.obj::
	$(CC) $(CFLAGS) $(LIBGJS_CFLAGS) /Fovs$(VSVER)\$(CFG)\$(PLAT)\libgjs\ /Fdvs$(VSVER)\$(CFG)\$(PLAT)\libgjs\ /c @<<
$<
<<

{..\modules\}.cpp{vs$(VSVER)\$(CFG)\$(PLAT)\libgjs\}.obj::
	$(CXX) $(CFLAGS) $(LIBGJS_CFLAGS) /Fovs$(VSVER)\$(CFG)\$(PLAT)\libgjs\ /Fdvs$(VSVER)\$(CFG)\$(PLAT)\libgjs\ /c @<<
$<
<<

{..\util\}.cpp{vs$(VSVER)\$(CFG)\$(PLAT)\libgjs\}.obj::
	$(CXX) $(CFLAGS) $(LIBGJS_CFLAGS) /Fovs$(VSVER)\$(CFG)\$(PLAT)\libgjs\ /Fdvs$(VSVER)\$(CFG)\$(PLAT)\libgjs\ /c @<<
$<
<<

{vs$(VSVER)\$(CFG)\$(PLAT)\module-resources\}.c{vs$(VSVER)\$(CFG)\$(PLAT)\libgjs\}.obj::
	$(CC) $(CFLAGS) $(LIBGJS_CFLAGS) /Fovs$(VSVER)\$(CFG)\$(PLAT)\libgjs\ /Fdvs$(VSVER)\$(CFG)\$(PLAT)\libgjs\ /c @<<
$<
<<

{..\gjs\}.cpp{vs$(VSVER)\$(CFG)\$(PLAT)\gjs-console\}.obj::
	$(CXX) $(CFLAGS) $(GJS_CFLAGS) /Fovs$(VSVER)\$(CFG)\$(PLAT)\gjs-console\ /Fdvs$(VSVER)\$(CFG)\$(PLAT)\gjs-console\ /c @<<
$<
<<

# Rules for building .lib files
vs$(VSVER)\$(CFG)\$(PLAT)\gjs.lib: $(LIBGJS_DLL_FILENAME).dll

# Rules for linking DLLs
# Format is as follows (the mt command is needed for MSVC 2005/2008 builds):
# $(dll_name_with_path): $(dependent_libs_files_objects_and_items)
#	link /DLL [$(linker_flags)] [$(dependent_libs)] [/def:$(def_file_if_used)] [/implib:$(lib_name_if_needed)] -out:$@ @<<
# $(dependent_objects)
# <<
# 	@-if exist $@.manifest mt /manifest $@.manifest /outputresource:$@;2
$(LIBGJS_DLL_FILENAME).dll:		\
$(GJS_INCLUDED_MODULES)			\
vs$(VSVER)\$(CFG)\$(PLAT)\module-resources		\
vs$(VSVER)\$(CFG)\$(PLAT)\libgjs			\
$(module_resources_generated_srcs)	\
$(libgjs_dll_OBJS)
	link /DLL $(LDFLAGS) $(GJS_INCLUDED_MODULES)		\
	$(LIBGJS_DEP_LIBS) /implib:vs$(VSVER)\$(CFG)\$(PLAT)\gjs.lib	\
	-out:$@ @<<
$(libgjs_dll_OBJS)
<<
	@-if exist $@.manifest mt /manifest $@.manifest /outputresource:$@;2

vs$(VSVER)\$(CFG)\$(PLAT)\module-console.lib: vs$(VSVER)\$(CFG)\$(PLAT)\libgjs\config.h vs$(VSVER)\$(CFG)\$(PLAT)\module-console $(module_console_OBJS)
	lib $(ARFLAGS) -out:$@ @<<
$(module_console_OBJS)
<<

vs$(VSVER)\$(CFG)\$(PLAT)\module-system.lib: vs$(VSVER)\$(CFG)\$(PLAT)\libgjs\config.h vs$(VSVER)\$(CFG)\$(PLAT)\module-system $(module_system_OBJS)
	lib $(ARFLAGS) -out:$@ @<<
$(module_system_OBJS)
<<

vs$(VSVER)\$(CFG)\$(PLAT)\module-cairo.lib: vs$(VSVER)\$(CFG)\$(PLAT)\libgjs\config.h vs$(VSVER)\$(CFG)\$(PLAT)\module-cairo $(module_cairo_OBJS)
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

vs$(VSVER)\$(CFG)\$(PLAT)\gjs-console.exe: vs$(VSVER)\$(CFG)\$(PLAT)\gjs.lib vs$(VSVER)\$(CFG)\$(PLAT)\gjs-console $(gjs_OBJS)
	link $(LDFLAGS) vs$(VSVER)\$(CFG)\$(PLAT)\gjs.lib $(GJS_BASE_LIBS) -out:$@ $(gjs_OBJS)
	@-if exist $@.manifest mt /manifest $@.manifest /outputresource:$@;1

clean:
	@-if exist vs$(VSVER)\$(CFG)\$(PLAT)\GjsPrivate-1.0.typelib del /f /q vs$(VSVER)\$(CFG)\$(PLAT)\GjsPrivate-1.0.typelib
	@-if exist vs$(VSVER)\$(CFG)\$(PLAT)\GjsPrivate-1.0.gir del /f /q vs$(VSVER)\$(CFG)\$(PLAT)\GjsPrivate-1.0.gir
	@-if exist vs$(VSVER)\$(CFG)\$(PLAT)\libgjs\gjs_private_list del /f /q vs$(VSVER)\$(CFG)\$(PLAT)\libgjs\gjs_private_list
	@-del /f /q vs$(VSVER)\$(CFG)\$(PLAT)\*.exe
	@-del /f /q vs$(VSVER)\$(CFG)\$(PLAT)\*.dll
	@-del /f /q vs$(VSVER)\$(CFG)\$(PLAT)\*.pdb
	@-del /f /q vs$(VSVER)\$(CFG)\$(PLAT)\*.ilk
	@-del /f /q vs$(VSVER)\$(CFG)\$(PLAT)\*.exp
	@-del /f /q vs$(VSVER)\$(CFG)\$(PLAT)\*.lib
	@-if exist vs$(VSVER)\$(CFG)\$(PLAT)\module-cairo\ del /f /q vs$(VSVER)\$(CFG)\$(PLAT)\module-cairo\vc$(PDBVER)0.pdb
	@-if exist vs$(VSVER)\$(CFG)\$(PLAT)\module-cairo\ del /f /q vs$(VSVER)\$(CFG)\$(PLAT)\module-cairo\*.obj
	@-if exist vs$(VSVER)\$(CFG)\$(PLAT)\module-cairo\ rd vs$(VSVER)\$(CFG)\$(PLAT)\module-cairo
	@-del /f /q vs$(VSVER)\$(CFG)\$(PLAT)\gjs-console\vc$(PDBVER)0.pdb
	@-del /f /q vs$(VSVER)\$(CFG)\$(PLAT)\gjs-console\*.obj
	@-rd vs$(VSVER)\$(CFG)\$(PLAT)\gjs-console
	@-del /f /q vs$(VSVER)\$(CFG)\$(PLAT)\libgjs\vc$(PDBVER)0.pdb
	@-del /f /q vs$(VSVER)\$(CFG)\$(PLAT)\libgjs\*.obj
	@-del /f /q vs$(VSVER)\$(CFG)\$(PLAT)\module-system\vc$(PDBVER)0.pdb
	@-del /f /q vs$(VSVER)\$(CFG)\$(PLAT)\module-system\*.obj
	@-rd vs$(VSVER)\$(CFG)\$(PLAT)\module-system
	@-del /f /q vs$(VSVER)\$(CFG)\$(PLAT)\module-console\vc$(PDBVER)0.pdb
	@-del /f /q vs$(VSVER)\$(CFG)\$(PLAT)\module-console\*.obj
	@-rd vs$(VSVER)\$(CFG)\$(PLAT)\module-console
	@-del /f /q $(module_resources_generated_srcs)
	@-rd vs$(VSVER)\$(CFG)\$(PLAT)\module-resources
	@-del vs$(VSVER)\$(CFG)\$(PLAT)\libgjs\config.h
	@-rd vs$(VSVER)\$(CFG)\$(PLAT)\libgjs
	@-del /f /q vc$(PDBVER)0.pdb
