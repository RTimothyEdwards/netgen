#ifndef _HASH_H
#define _HASH_H

struct hashlist {
  char *name;
  void *ptr;
  struct hashlist *next;
};

extern void InitializeHashTable(struct hashlist **tab, int size);
extern int RecurseHashTable(struct hashlist **hashtab, int hashsize,
	int (*func)(struct hashlist *elem));
extern int RecurseHashTableValue(struct hashlist **hashtab, int hashsize,
	int (*func)(struct hashlist *elem, int), int);
extern struct nlist *RecurseHashTablePointer(struct hashlist **hashtab,
	int hashsize, struct nlist *(*func)(struct hashlist *elem,
	void *), void *pointer);


extern int CountHashTableEntries(struct hashlist *p);
extern int CountHashTableBinsUsed(struct hashlist *p);
extern void HashDelete(char *name, struct hashlist **hashtab, int hashsize);
extern void HashIntDelete(char *name, int value, struct hashlist **hashtab,
		int hashsize);

/* these functions return a pointer to a hash list element */
extern struct hashlist *HashInstall(char *name, struct hashlist **hashtab,
		int hashsize);
extern struct hashlist *HashPtrInstall(char *name, void *ptr, 
		struct hashlist **hashtab, int hashsize);
extern struct hashlist *HashIntPtrInstall(char *name, int value, void *ptr, 
		struct hashlist **hashtab, int hashsize);
extern struct hashlist *HashInt2PtrInstall(char *name, int c, void *ptr, 
		struct hashlist **hashtab, int hashsize);

/* these functions return the ->ptr field of a struct hashlist */
extern void *HashLookup(char *s, struct hashlist **hashtab, int hashsize);
extern void *HashIntLookup(char *s, int i, struct hashlist **hashtab, int hashsize);
extern void *HashInt2Lookup(char *s, int c, struct hashlist **hashtab, int hashsize);
extern void *HashFirst(struct hashlist **hashtab, int hashsize);
extern void *HashNext(struct hashlist **hashtab, int hashsize);

extern unsigned long hashnocase(char *s, int hashsize);
extern unsigned long hash(char *s, int hashsize);

extern int (*matchfunc)(char *, char *);
/* matchintfunc() compares based on the name and the first	*/
/* entry of the pointer value, which is cast as an integer	*/
extern int (*matchintfunc)(char *, char *, int, int);
extern unsigned long (*hashfunc)(char *, int);

#endif /* _HASH_H */
