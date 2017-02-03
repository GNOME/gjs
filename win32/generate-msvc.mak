# NMake Makefile portion for code generation and
# intermediate build directory creation
# Items in here should not need to be edited unless
# one is maintaining the NMake build files.

# Copy the pre-defined config.h.win32
..\config.h: config.h.win32
	@-copy $(@B).h.win32 $@

# Create the build directories
$(CFG)\$(PLAT)\module-console	\
$(CFG)\$(PLAT)\module-system	\
$(CFG)\$(PLAT)\module-resources	\
$(CFG)\$(PLAT)\module-cairo	\
$(CFG)\$(PLAT)\libgjs		\
$(CFG)\$(PLAT)\gjs-console:
	@-mkdir $@

# Generate the GResource sources
$(CFG)\$(PLAT)\module-resources\modules-resources.h	\
$(CFG)\$(PLAT)\module-resources\modules-resources.c: ..\modules\modules.gresource.xml
	$(PREFIX)\bin\glib-compile-resources.exe --target=$@	\
	--sourcedir=.. --generate --c-name modules_resources	\
	$**
