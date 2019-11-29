
!if "$(BUILD_INTROSPECTION)" == "TRUE"
# Create the file list for introspection (to avoid the dreaded command-line-too-long problem on Windows)
vs$(VSVER)\$(CFG)\$(PLAT)\libgjs\gjs_private_list:
	@for %f in ($(LIBGJS_PRIVATE_SOURCES)) do @echo ../%f >> $@

vs$(VSVER)\$(CFG)\$(PLAT)\GjsPrivate-1.0.gir: vs$(VSVER)\$(CFG)\$(PLAT)\gjs.lib vs$(VSVER)\$(CFG)\$(PLAT)\libgjs\gjs_private_list
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
	--add-include-path=$(G_IR_INCLUDEDIR)		\
	--pkg-export=gjs				\
	--cflags-begin					\
	$(LIBGJS_CFLAGS_BASE)				\
	--cflags-end					\
	--filelist=vs$(VSVER)\$(CFG)\$(PLAT)\libgjs\gjs_private_list	\
	-L.\vs$(VSVER)\$(CFG)\$(PLAT)	\
	-o $@

vs$(VSVER)\$(CFG)\$(PLAT)\GjsPrivate-1.0.typelib: vs$(VSVER)\$(CFG)\$(PLAT)\GjsPrivate-1.0.gir
	$(PREFIX)\bin\g-ir-compiler			\
	--includedir=vs$(VSVER)\$(CFG)\$(PLAT) --debug --verbose	\
	$(**:\=/)					\
	-o $@
!else
!error $(ERROR_MSG)
!endif
