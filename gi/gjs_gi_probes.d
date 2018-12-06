provider gjs {
	probe object__wrapper__new(void*, void*, char *, char *);
	probe object__wrapper__finalize(void*, void*, char *, char *);
};
