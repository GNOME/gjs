# NMake Makefile portion for code generation and
# intermediate build directory creation
# Items in here should not need to be edited unless
# one is maintaining the NMake build files.

# Copy the pre-defined config.h.win32
vs$(VSVER)\$(CFG)\$(PLAT)\libgjs\config.h: config.h.win32 vs$(VSVER)\$(CFG)\$(PLAT)\libgjs
	@-copy $(@B).h.win32 $@

# Create the build directories
vs$(VSVER)\$(CFG)\$(PLAT)\module-console	\
vs$(VSVER)\$(CFG)\$(PLAT)\module-system	\
vs$(VSVER)\$(CFG)\$(PLAT)\module-resources	\
vs$(VSVER)\$(CFG)\$(PLAT)\module-cairo	\
vs$(VSVER)\$(CFG)\$(PLAT)\libgjs		\
vs$(VSVER)\$(CFG)\$(PLAT)\gjs-console:
	@-mkdir $@

# Generate the GResource sources
vs$(VSVER)\$(CFG)\$(PLAT)\module-resources\modules-resources.h	\
vs$(VSVER)\$(CFG)\$(PLAT)\module-resources\modules-resources.c: ..\modules\modules.gresource.xml
	$(PREFIX)\bin\glib-compile-resources.exe --target=$@	\
	--sourcedir=.. --generate --c-name modules_resources	\
	$**
