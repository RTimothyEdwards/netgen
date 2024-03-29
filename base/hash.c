/* "NETGEN", a netlist-specification tool for VLSI
   Copyright (C) 1989, 1990   Massimo A. Sivilotti
   Author's address: mass@csvax.cs.caltech.edu;
                     Caltech 256-80, Pasadena CA 91125.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation (any version).

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file copying.  If not, write to
the Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

/* hash.c  -- hash table support functions  */

#include "config.h"

#include <stdio.h>
#ifdef IBMPC
#include <alloc.h>
#endif

#ifdef TCL_NETGEN
#include <tcl.h>
#endif

#include "netgen.h"
#include "objlist.h"
#include "hash.h"

unsigned long (*hashfunc)(char *, int) = NULL;
int (*matchfunc)(char *, char *) = NULL;
int (*matchintfunc)(char *, char *, int, int) = NULL;

void InitializeHashTable(struct hashdict *dict, int size)
{
    struct hashlist **hashtab;

    dict->hashtab = (struct hashlist **)CALLOC(size, sizeof(struct hashlist *));
    dict->hashsize = size;
    dict->hashfirstindex = 0;
    dict->hashfirstptr = NULL;
}

int RecurseHashTable(struct hashdict *dict, int (*func)(struct hashlist *elem))
/* returns the sum of the return values of (*func) */
{
	int i, sum;
	struct hashlist *p;
	
	sum = 0;
	for (i = 0; i < dict->hashsize; i++)
		for (p = dict->hashtab[i]; p != NULL; p = p->next) 
			sum += (*func)(p);
	return(sum);
}

/* Variation on RecurseHashTable() that passes an additional
 * type int value to the function.
 */

int RecurseHashTableValue(struct hashdict *dict,
	int (*func)(struct hashlist *elem, int), int value)
{
	int i, sum;
	struct hashlist *p;
	
	sum = 0;
	for (i = 0; i < dict->hashsize; i++)
		for (p = dict->hashtab[i]; p != NULL; p = p->next) 
			sum += (*func)(p, value);
	return(sum);
}

/* Another variation on RecurseHashTable() that passes one pointer
 * type value to the function, so that the pointer may be to a
 * structure, allowing any number of values to be passed to the
 * function through that structure.
 */

struct nlist *RecurseHashTablePointer(struct hashdict *dict,
	struct nlist *(*func)(struct hashlist *elem, void *),
	void *pointer)
{
    int i;
    struct hashlist *p;
    struct nlist *tp;
 
    for (i = 0; i < dict->hashsize; i++) {
	for (p = dict->hashtab[i]; p != NULL; p = p->next) {
	    tp = (*func)(p, pointer);
	    if (tp != NULL) return tp;
	}
    }

    return NULL;
}

int CountHashTableEntries(struct hashlist *p)
{
	/* not strictly needed, but stops compiler from bitching */
	return ((p != NULL) ? 1:0);
}

int CountHashTableBinsUsed(struct hashlist *p)
{
	if (p->next == NULL) return (1);
	return(0);
}

static unsigned char uppercase[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
	0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
	0x60, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
	0x58, 0x59, 0x5a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f
};

// Hash functions of the stupid-simple accumulate-the-character-codes
// method replaced by the more sophisticated SDBM hash.  Otherwise
// horrible things can happen, as, for example, names AOI12 and OAI12
// have exactly the same hash result.  Lousy for binning and even
// lousier for generating class magic numbers.

unsigned long hashnocase(char *s, int hashsize)
{
	unsigned long hashval;
	
	for (hashval = 0; *s != '\0'; )
	    hashval = uppercase[*s++]
			+ (hashval << 6) + (hashval << 16) - hashval;
	return (hashsize == 0) ? hashval : (hashval % hashsize);
}

unsigned long hashcase(char *s, int hashsize)
{
	unsigned long hashval;
	
	for (hashval = 0; *s != '\0'; )
	    hashval = (*s++) + (hashval << 6) + (hashval << 16) - hashval;
	return (hashsize == 0) ? hashval : (hashval % hashsize);
}

unsigned long genhash(char *s, int c, int hashsize)
{
	unsigned long hashval;
	
	for (hashval = (unsigned long)c; *s != '\0'; )
	    hashval = (*s++) + (hashval << 6) + (hashval << 16) - hashval;
	return (hashsize == 0) ? hashval : (hashval % hashsize);
}

/*----------------------------------------------------------------------*/
/* HashLookup --							*/
/* return the 'ptr' field of the hash table entry, or NULL if not found */
/*----------------------------------------------------------------------*/

void *HashLookup(char *s, struct hashdict *dict)
{
  struct hashlist *np;
  unsigned long hashval;
	
  hashval = (*hashfunc)(s, dict->hashsize);
	
  for (np = dict->hashtab[hashval]; np != NULL; np = np->next)
    if ((*matchfunc)(s, np->name)) return (np->ptr);	/* correct match */
  return (NULL); /* not found */
}

/*----------------------------------------------------------------------*/
/* HashIntLookup --							*/
/* return the 'ptr' field of the hash table entry, or NULL if not found */
/* HashIntLookup treats *ptr as an integer and compares it to the	*/
/* passed integer value i						*/
/*----------------------------------------------------------------------*/

void *HashIntLookup(char *s, int i, struct hashdict *dict)
{
  struct hashlist *np;
  unsigned long hashval;
	
  hashval = (*hashfunc)(s, dict->hashsize);

  for (np = dict->hashtab[hashval]; np != NULL; np = np->next) {
    if (np->ptr == NULL) {
       if ((*matchintfunc)(s, np->name, i, -1))
	  return NULL;
    }
    else {
       if ((*matchintfunc)(s, np->name, i, (int)(*((int *)np->ptr))))
	  return (np->ptr);	/* correct match */
    }
  }
  return (NULL); /* not found */
}

/*----------------------------------------------------------------------*/
/* Similar to HashIntLookup, but HashInt2Lookup adds the integer c as	*/
/* part of the hash, using a special hash function to hash the char	*/
/* first, then the character string.					*/
/*----------------------------------------------------------------------*/

void *HashInt2Lookup(char *s, int c, struct hashdict *dict)
{
  struct hashlist *np;
  unsigned long hashval;
	
  hashval = genhash(s, c, dict->hashsize);
	
  for (np = dict->hashtab[hashval]; np != NULL; np = np->next)
    if (!strcmp(s, np->name))
      return (np->ptr);	/* correct match */

  return (NULL); /* not found */
}


/*----------------------------------------------------------------------*/
/* return the hashlist entry, after (re)initializing its 'ptr' field */
/*----------------------------------------------------------------------*/

struct hashlist *HashPtrInstall(char *name, void *ptr, struct hashdict *dict)
{
  struct hashlist *np;
  unsigned long hashval;
	
  hashval = (*hashfunc)(name, dict->hashsize);
  for (np =  dict->hashtab[hashval]; np != NULL; np = np->next)
    if ((*matchfunc)(name, np->name)) {
      np->ptr = ptr;
      return (np);		/* match found in hash table */
    }

  /* not in table, so install it */
  if ((np = (struct hashlist *) CALLOC(1,sizeof(struct hashlist))) == NULL)
    return (NULL);
  if ((np->name = strsave(name)) == NULL) return (NULL);
  np->ptr = ptr;
  np->next =  dict->hashtab[hashval];
  return(dict->hashtab[hashval] = np);
}

/*----------------------------------------------------------------------*/
/* Like HashLookup, a separate routine is needed when using an		*/
/* additional value for the matching.					*/
/*----------------------------------------------------------------------*/

struct hashlist *HashIntPtrInstall(char *name, int value, void *ptr,
			struct hashdict *dict)
{
  struct hashlist *np;
  unsigned long hashval;
	
  hashval = (*hashfunc)(name, dict->hashsize);
  for (np =  dict->hashtab[hashval]; np != NULL; np = np->next)
    if ((*matchintfunc)(name, np->name, value, (int)(*((int *)np->ptr)))) {
      np->ptr = ptr;
      return (np);		/* match found in hash table */
    }

  /* not in table, so install it */
  if ((np = (struct hashlist *) CALLOC(1,sizeof(struct hashlist))) == NULL)
    return (NULL);
  if ((np->name = strsave(name)) == NULL) return (NULL);
  np->ptr = ptr;
  np->next = dict->hashtab[hashval];
  return(dict->hashtab[hashval] = np);
}

/*----------------------------------------------------------------------*/
/* And the following is used with HashInt2.			*/
/*----------------------------------------------------------------------*/

struct hashlist *HashInt2PtrInstall(char *name, int c, void *ptr,
			struct hashdict *dict)
{
  struct hashlist *np;
  unsigned long hashval;
	
  hashval = genhash(name, c, dict->hashsize);
  for (np = dict->hashtab[hashval]; np != NULL; np = np->next)
    if (!strcmp(name, np->name)) {
      np->ptr = ptr;
      return (np);		/* match found in hash table */
    }

  /* not in table, so install it */
  if ((np = (struct hashlist *) CALLOC(1,sizeof(struct hashlist))) == NULL)
    return (NULL);
  if ((np->name = strsave(name)) == NULL) return (NULL);
  np->ptr = ptr;
  np->next = dict->hashtab[hashval];
  return(dict->hashtab[hashval] = np);
}

/*----------------------------------------------------------------------*/
/* destroy a hash table, freeing associated memory 			*/
/*----------------------------------------------------------------------*/

void HashKill(struct hashdict *dict)
{
  struct hashlist *np, *p;
  int i;

  if (dict->hashtab == NULL) return;	// Hash not initialized

  for (i = 0; i < dict->hashsize; i++) {
    for (p = dict->hashtab[i]; p != NULL; ) {
      np = p->next;
      FREE(p->name);
      FREE(p);
      p = np;
    }
  }
  FREE(dict->hashtab);
  dict->hashtab = NULL;
}

/*----------------------------------------------------------------------*/
/* Basic hash install, leaving a NULL pointer but returning a pointer	*/
/* to the new hash entry.						*/
/*----------------------------------------------------------------------*/

struct hashlist *HashInstall(char *name, struct hashdict *dict)
{
  struct hashlist *np;
  unsigned long hashval;
	
  hashval = (*hashfunc)(name, dict->hashsize);
  for (np = dict->hashtab[hashval]; np != NULL; np = np->next)
    if ((*matchfunc)(name, np->name)) return (np); /* match found in hash table */

  /* not in table, so install it */
  if ((np = (struct hashlist *) CALLOC(1,sizeof(struct hashlist))) == NULL)
    return (NULL);
  if ((np->name = strsave(name)) == NULL) return (NULL);
  np->ptr = NULL;
  np->next = dict->hashtab[hashval];
  return(dict->hashtab[hashval] = np);
}

/*----------------------------------------------------------------------*/
/* frees a hash table entry, (but not the 'ptr' field) 			*/
/*----------------------------------------------------------------------*/

void HashDelete(char *name, struct hashdict *dict)
{
  unsigned long hashval;
  struct hashlist *np;
  struct hashlist *np2;
  
  hashval = (*hashfunc)(name, dict->hashsize);
  np = dict->hashtab[hashval];
  if (np == NULL) return;

  if ((*matchfunc)(name, np->name)) {
    /* it is the first element in the list */
    dict->hashtab[hashval] = np->next;
    FREE(np->name);
    FREE(np);
    return;
  }

  /* else, traverse the list, deleting the appropriate element */
  while (np->next != NULL) {
    if ((*matchfunc)(name, np->next->name)) {
      np2 = np->next;
      np->next = np2->next;
      FREE(np2->name);
      FREE(np2);
      return;
    }
    np = np->next;
  }
}

/*----------------------------------------------------------------------*/
/* HashDelete with additional integer value matching			*/
/*----------------------------------------------------------------------*/

void HashIntDelete(char *name, int value, struct hashdict *dict)
{
  unsigned long hashval;
  struct hashlist *np;
  struct hashlist *np2;
  
  hashval = (*hashfunc)(name, dict->hashsize);
  np = dict->hashtab[hashval];
  if (np == NULL) return;

  if ((*matchintfunc)(name, np->name, value, (int)(*((int *)np->ptr)))) {
    /* it is the first element in the list */
    dict->hashtab[hashval] = np->next;
    FREE(np->name);
    FREE(np);
    return;
  }

  /* else, traverse the list, deleting the appropriate element */
  while (np->next != NULL) {
    if ((*matchintfunc)(name, np->next->name, value,
		(int)(*((int *)np->next->ptr)))) {
      np2 = np->next;
      np->next = np2->next;
      FREE(np2->name);
      FREE(np2);
      return;
    }
    np = np->next;
  }
}

/*----------------------------------------------------------------------*/
/* Hash key iterator							*/
/* returns 'ptr' field of next element, NULL when done 			*/
/*----------------------------------------------------------------------*/

void *HashNext(struct hashdict *dict)
{
   if (dict->hashfirstptr != NULL && dict->hashfirstptr->next != NULL) {
      dict->hashfirstptr = dict->hashfirstptr->next;
      return(dict->hashfirstptr->ptr);
   }
   while (dict->hashfirstindex < dict->hashsize) {
      if ((dict->hashfirstptr = dict->hashtab[dict->hashfirstindex++]) != NULL) {
         return(dict->hashfirstptr->ptr);
      }
   }

   dict->hashfirstindex = 0;
   dict->hashfirstptr = NULL;
   return(NULL);
}

/*----------------------------------------------------------------------*/
/* Hash key iterator setup						*/
/*----------------------------------------------------------------------*/

void *HashFirst(struct hashdict *dict)
{
   dict->hashfirstindex = 0;
   dict->hashfirstptr = NULL;
   return HashNext(dict);
}
