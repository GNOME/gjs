# NMake Makefile snippet for copying the built libraries, utilities and headers to
# a path under $(PREFIX).

install: all
	@if not exist $(PREFIX)\bin\ mkdir $(PREFIX)\bin
	@if not exist $(PREFIX)\lib\ mkdir $(PREFIX)\lib
	@if not exist $(PREFIX)\include\gjs-1.0\gjs @mkdir $(PREFIX)\include\gjs-1.0\gjs
	@if not exist $(PREFIX)\include\gjs-1.0\util @mkdir $(PREFIX)\include\gjs-1.0\util
	@copy /b $(LIBGJS_DLL_FILENAME).dll $(PREFIX)\bin
	@copy /b $(LIBGJS_DLL_FILENAME).pdb $(PREFIX)\bin
	@copy /b vs$(VSVER)\$(CFG)\$(PLAT)\gjs.lib $(PREFIX)\lib
	@copy /b vs$(VSVER)\$(CFG)\$(PLAT)\gjs-console.exe $(PREFIX)\bin
	@copy /b vs$(VSVER)\$(CFG)\$(PLAT)\gjs-console.exe $(PREFIX)\bin\gjs.exe
	@copy /b vs$(VSVER)\$(CFG)\$(PLAT)\gjs-console.pdb $(PREFIX)\bin
	@for %h in ($(LIBGJS_HEADERS)) do @copy ..\%h $(PREFIX)\include\gjs-1.0\%h
	@rem Copy the generated introspection files, if built
	@if exist vs$(VSVER)\$(CFG)\$(PLAT)\GjsPrivate-1.0.gir copy vs$(VSVER)\$(CFG)\$(PLAT)\GjsPrivate-1.0.gir $(PREFIX)\share\gir-1.0
	@if exist vs$(VSVER)\$(CFG)\$(PLAT)\GjsPrivate-1.0.typelib copy /b vs$(VSVER)\$(CFG)\$(PLAT)\GjsPrivate-1.0.typelib $(PREFIX)\lib\girepository-1.0
