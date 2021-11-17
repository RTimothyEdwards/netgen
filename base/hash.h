#ifndef _HASH_H
#define _HASH_H

struct hashlist {
  char *name;
  void *ptr;
  struct hashlist *next;
};

struct hashdict {
  int hashsize;
  int hashfirstindex;
  struct hashlist *hashfirstptr;
  struct hashlist **hashtab;
};


extern void InitializeHashTable(struct hashdict *dict, int size);
extern int RecurseHashTable(struct hashdict *dict,
	int (*func)(struct hashlist *elem));
extern int RecurseHashTableValue(struct hashdict *dict,
	int (*func)(struct hashlist *elem, int), int);
extern struct nlist *RecurseHashTablePointer(struct hashdict *dict,
	struct nlist *(*func)(struct hashlist *elem, void *),
	void *pointer);
extern void HashDelete(char *name, struct hashdict *dict);
extern void HashIntDelete(char *name, int value, struct hashdict *dict);
extern void HashKill(struct hashdict *dict);


extern int CountHashTableEntries(struct hashlist *p);
extern int CountHashTableBinsUsed(struct hashlist *p);

/* these functions return a pointer to a hash list element */
extern struct hashlist *HashInstall(char *name, struct hashdict *dict);
extern struct hashlist *HashPtrInstall(char *name, void *ptr, 
		struct hashdict *dict);
extern struct hashlist *HashIntPtrInstall(char *name, int value, void *ptr, 
		struct hashdict *dict);
extern struct hashlist *HashInt2PtrInstall(char *name, int c, void *ptr, 
		struct hashdict *dict);

/* these functions return the ->ptr field of a struct hashlist */
extern void *HashLookup(char *s, struct hashdict *dict);
extern void *HashIntLookup(char *s, int i, struct hashdict *dict);
extern void *HashInt2Lookup(char *s, int c, struct hashdict *dict);
extern void *HashFirst(struct hashdict *dict);
extern void *HashNext(struct hashdict *dict);

extern unsigned long hashnocase(char *s, int hashsize);
extern unsigned long my_hash(char *s, int hashsize);

extern int (*matchfunc)(char *, char *);
/* matchintfunc() compares based on the name and the first	*/
/* entry of the pointer value, which is cast as an integer	*/
extern int (*matchintfunc)(char *, char *, int, int);
extern unsigned long (*hashfunc)(char *, int);

#endif /* _HASH_H */
