
!if "$(BUILD_INTROSPECTION)" == "TRUE"
# Create the file list for introspection (to avoid the dreaded command-line-too-long problem on Windows)
$(CFG)\$(PLAT)\gjs_private_list:
	@for %f in ($(LIBGJS_PRIVATE_SOURCES)) do @echo ../%f >> $@

$(CFG)\$(PLAT)\GjsPrivate-1.0.gir: $(CFG)\$(PLAT)\gjs.lib $(CFG)\$(PLAT)\gjs_private_list
	@set LIB=.\$(CFG)\$(PLAT);$(PREFIX)\lib;$(LIB)
	@set PATH=.\$(CFG)\$(PLAT);$(PREFIX)\bin;$(PATH)
	@-echo Generating $@...
	$(PYTHON) $(G_IR_SCANNER)			\
	--verbose -no-libtool				\
	--identifier-prefix=Gjs				\
	--symbol-prefix=gjs_				\
	--warn-all					\
	--namespace=GjsPrivate			\
	--nsversion=1.0					\
	$(INTROSPECTION_INCLUDE_PACKAGES)		\
	--library=gjs					\
	--library-path=$(CFG)\$(PLAT)		\
	--add-include-path=$(G_IR_INCLUDEDIR)		\
	--pkg-export=gjs				\
	--cflags-begin					\
	$(CFLAGS) $(LIBGJS_CFLAGS)			\
	--cflags-end					\
	--filelist=$(CFG)\$(PLAT)\gjs_private_list	\
	-o $@

$(CFG)\$(PLAT)\GjsPrivate-1.0.typelib: $(CFG)\$(PLAT)\GjsPrivate-1.0.gir
	@copy $*.gir $(@B).gir
	$(PREFIX)\bin\g-ir-compiler			\
	--includedir=$(CFG)\$(PLAT) --debug --verbose	\
	$(@B).gir					\
	-o $@
	@del $(@B).gir
!else
!error $(ERROR_MSG)
!endif
