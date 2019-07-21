# Convert the source listing to object (.obj) listing in
# another NMake Makefile module, include it, and clean it up.
# This is a "fact-of-life" regarding NMake Makefiles...
# This file does not need to be changed unless one is maintaining the NMake Makefiles

# For those wanting to add things here:
# To add a list, do the following:
# # $(description_of_list)
# if [call create-lists.bat header $(makefile_snippet_file) $(variable_name)]
# endif
#
# if [call create-lists.bat file $(makefile_snippet_file) $(file_name)]
# endif
#
# if [call create-lists.bat footer $(makefile_snippet_file)]
# endif
# ... (repeat the if [call ...] lines in the above order if needed)
# !include $(makefile_snippet_file)
#
# (add the following after checking the entries in $(makefile_snippet_file) is correct)
# (the batch script appends to $(makefile_snippet_file), you will need to clear the file unless the following line is added)
#!if [del /f /q $(makefile_snippet_file)]
#!endif

# In order to obtain the .obj filename that is needed for NMake Makefiles to build DLLs/static LIBs or EXEs, do the following
# instead when doing 'if [call create-lists.bat file $(makefile_snippet_file) $(file_name)]'
# (repeat if there are multiple $(srcext)'s in $(source_list), ignore any headers):
# !if [for %c in ($(source_list)) do @if "%~xc" == ".$(srcext)" @call create-lists.bat file $(makefile_snippet_file) $(intdir)\%~nc.obj]
#
# $(intdir)\%~nc.obj needs to correspond to the rules added in build-rules-msvc.mak
# %~xc gives the file extension of a given file, %c in this case, so if %c is a.cc, %~xc means .cc
# %~nc gives the file name of a given file without extension, %c in this case, so if %c is a.cc, %~nc means a

NULL=

# For libgjs

!if [call create-lists.bat header gjs_objs.mak libgjs_dll_OBJS]
!endif

!if [for %c in ($(LIBGJS_SOURCES)) do @if "%~xc" == ".cpp" @call create-lists.bat file gjs_objs.mak vs^$(VSVER)\^$(CFG)\^$(PLAT)\libgjs\%~nc.obj]
!endif

!if [for %c in ($(LIBGJS_SOURCES)) do @if "%~xc" == ".c" @call create-lists.bat file gjs_objs.mak vs^$(VSVER)\^$(CFG)\^$(PLAT)\libgjs\%~nc.obj]
!endif

!if [for %c in ($(module_resource_srcs)) do @if "%~xc" == ".c" @call create-lists.bat file gjs_objs.mak vs^$(VSVER)\^$(CFG)\^$(PLAT)\libgjs\%~nc.obj]
!endif

!if [call create-lists.bat footer gjs_objs.mak]
!endif

!if [call create-lists.bat header gjs_objs.mak gjs_OBJS]
!endif

!if [for %c in ($(gjs_console_srcs)) do @if "%~xc" == ".cpp" @call create-lists.bat file gjs_objs.mak vs^$(VSVER)\^$(CFG)\^$(PLAT)\gjs-console\%~nc.obj]
!endif

!if [call create-lists.bat footer gjs_objs.mak]
!endif

!include gjs_objs.mak

!if [del /f /q gjs_objs.mak]
!endif

# For module-resources
!if [call create-lists.bat header gjs_modules_objs.mak module_resources_generated_srcs]
!endif

!if [for %c in ($(module_resource_srcs)) do @call create-lists.bat file gjs_modules_objs.mak vs^$(VSVER)\^$(CFG)\^$(PLAT)\module-resources\%c]
!endif

!if [call create-lists.bat footer gjs_modules_objs.mak]
!endif

!if [call create-lists.bat header gjs_modules_objs.mak module_system_OBJS]
!endif

!if [for %c in ($(module_system_srcs)) do @if "%~xc" == ".cpp" @call create-lists.bat file gjs_modules_objs.mak vs^$(VSVER)\^$(CFG)\^$(PLAT)\module-system\%~nc.obj]
!endif

!if [call create-lists.bat footer gjs_modules_objs.mak]
!endif

!if [call create-lists.bat header gjs_modules_objs.mak module_console_OBJS]
!endif

!if [for %c in ($(module_console_srcs)) do @if "%~xc" == ".cpp" @call create-lists.bat file gjs_modules_objs.mak vs^$(VSVER)\^$(CFG)\^$(PLAT)\module-console\%~nc.obj]
!endif

!if [call create-lists.bat footer gjs_modules_objs.mak]
!endif

!if [call create-lists.bat header gjs_modules_objs.mak module_cairo_OBJS]
!endif

!if [for %c in ($(module_cairo_srcs)) do @if "%~xc" == ".cpp" @call create-lists.bat file gjs_modules_objs.mak vs^$(VSVER)\^$(CFG)\^$(PLAT)\module-cairo\%~nc.obj]
!endif

!if [call create-lists.bat footer gjs_modules_objs.mak]
!endif

!include gjs_modules_objs.mak

!if [del /f /q gjs_modules_objs.mak]
!endif
