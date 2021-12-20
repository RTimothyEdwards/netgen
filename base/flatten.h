#ifndef _FLATTEN_H
#define _FLATTEN_H

extern int UniquePins(char *name, int filenum);
extern void flattenCell(char *name, int file);
extern int HasContents(struct nlist *);

#endif /* _FLATTEN_H */
