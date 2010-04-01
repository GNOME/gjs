provider gjs {
	probe object__proxy__new(void*, void*, char *, char *);
	probe object__proxy__finalize(void*, void*, char *, char *);
};
