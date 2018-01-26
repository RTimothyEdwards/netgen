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

/*  xilinx.c -- Output routines for Xilinx's .xnf format */

#include "config.h"

#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

#ifdef IBMPC
#include <stdlib.h>  /* for strtol on PC */
#endif

#include "netgen.h"
#include "objlist.h"
#include "netfile.h"
#include "hash.h"
#include "print.h"

#define XILINXHASHSIZE 99
static long xilinxhashbase = 0xA00;
static struct hashdict xilinxnamedict;
static FILE *xilinxfile;

char *gndnet = "0";
char *vccnet = "1";

char *xilinx_pin(s)
	char *s;
{
	static char buf[80];
	char *cp;
	int inpar; 

	inpar = 0;
	cp = NULL;
	while(*s){
		if(*s == '('){
			cp = buf;
			inpar++;
		}else if(*s == ')'){
			inpar++;
		}else if(inpar){
			*cp++ = *s;
		}
		s++;
	}
	if(cp){
		*cp = '\0';
		return(buf);
	}else
		return(NULL);
}
char *xilinx_name(prefix,s)
	char *prefix;
	char *s;
{
	static char buf[80];
	char *cp;
	int inpar;

	inpar = 0;
	cp = buf;
	while(*prefix)
		*cp++ = *prefix++;
	while(*s){
		if(*s == '('){
			inpar++;
		}else if(*s == ')'){
			inpar--;
		}else if(*s >= 'a' && *s <= 'z'){
			if(!inpar) *cp++ = *s;
		}else if(*s >= 'A' && *s <= 'Z'){
			if(!inpar) *cp++ = *s - 'A' + 'a';
		}else if(*s >= '0' && *s <= '9'){
			if(!inpar) *cp++ = *s;
		}else{
			if(!inpar) *cp++ = '$';
		}
		s++;
	}
	*cp = '\0';
	return(buf);
}

struct cname {
	int n;
	char *actel;
	char *xilinx;
} cname[] = {
	{3, "DF1","DFF"},
	{4, "DFC1","DFF"},
	{3, "DFE","DFF"},
	{6, "CLKBUF","ACLK"},
	{5, "CLOCK","OSC"},
	{5, "INBUF","IBUF"},
	{6, "OUTBUF","OBUF"},
	{5, "BIBUF","XXXX"},
	{6, "TRIBUF","OBUFT"},
	{2, "OR","OR"},
	{3, "AND","AND"},
	{3, "NOR","NOR"},
	{4, "NAND","NAND"},
	{0,NULL,NULL}
};
char *xilinx_class(model)
	char *model;
{
	struct cname *cnp;
	cnp = cname;
	while(cnp->n){
		if(!strncmp(cnp->actel,model,cnp->n))
			return(cnp->xilinx);
		cnp++;
	}
	return(model);
}
void
Xilinx(cellname, filename)
	char *cellname;
	char *filename;
{
	char Path[500];
	char FileName[500];

	if(LookupCell(cellname) == NULL){
		Printf("No such cell name: %s\n", cellname);
		return;
	}

	if(filename == NULL || strlen(filename) == 0)
		strcpy(Path, cellname);
	else
		strcpy(Path, filename);
	
	SetExtension(FileName, Path, XILINX_EXTENSION);
	if (!OpenFile(FileName, 80)){
		Printf("Failed to open file named: %s\n",FileName);
		perror("Xilinx(): Unable to open output file.");
		return;
	}
	ClearDumpedList();
	InitializeHashTable(&xilinxnamedict, XILINXHASHSIZE);
	if (LookupCell(cellname) != NULL)
		xilinxCell(cellname);
	CloseFile(FileName);
}

xilinxCell(cell)
	char *cell;
{
	struct nlist *nl;
	struct objlist *ob;
	struct objlist *xilinx_gate();
	char *pname,dir;
	int pin;
	long t;

	flattenCell(cell, -1);
	nl = LookupCell(cell);


	if(!nl)
		return(0);

	if (nl->class != CLASS_SUBCKT)
		return(0);

	time(&t);

	FlushString("LCANET, 2\n");
	FlushString("PROG, ntk2xnf, Created from %s %s",
		nl->name,
		ctime(&t)
	);
	ob = nl->cell;
	while(ob){
		/* a gate, collect the ports */
		if(ob->type == FIRSTPIN){
			ob = xilinx_gate(ob,nl);
		}else
			ob = ob->next;
	}
	ob = nl->cell;
	while(ob){
		if(ob->type == -91 || ob->type == -92 || ob->type == -93){
			pname = xilinx_pin(ob->name);
			switch(ob->type){
				case -91: dir = 'I'; break;
				case -92: dir = 'O'; break;
				case -93: dir = 'B'; break;
				default: dir = 'U'; break;
			}
			
			if(pname){
				if(*pname){
					FlushString("EXT,%s,%c,,LOC=%s\n",
						xilinx_name("n$",ob->name),
						dir,
						pname
					);
				}else{
					FlushString("EXT,%s,%c,,\n",
						xilinx_name("n$",ob->name),
						dir
					);
				}
			}
		}
		ob = ob->next;
	}
	FlushString("PWR,1,%s\n",xilinx_name("n$",vccnet));
	FlushString("PWR,0,%s\n",xilinx_name("n$",gndnet));
	FlushString("EOF\n");
	nl->dumped = 1;
	return(1);
}


struct objlist *xilinx_gate(ob,nl)
	struct objlist *ob;
	struct nlist *nl;
{
	struct nlist *gnl;
	struct objlist *nob;
	struct pins *pl,*npl;

	nob = ob;
	while(nob){
		nob = nob->next;
		if(nob && nob->type <= FIRSTPIN)
			break;
	}
	xilinx_sym(nl,ob);
	return(nob);
}

xilinx_sym(nl,gob)
	struct nlist *nl;
	struct objlist *gob;
{
	struct objlist *ob;
	char *cp,*rindex();
	int pin;
	struct objlist *xx;
	char *inv,dir,*net;

	FlushString("SYM,%s,%s\n",
		xilinx_name("",gob->instance.name),
		xilinx_class(gob->model.class)
	);

	ob = gob;
	pin = 0;
	while(ob){
		if(ob->type <= pin)
			break;
		pin = ob->type;
		cp  = rindex(ob->name,'/');
		cp++;

		switch(*cp){
			case '!':
				inv = "INV"; cp++; break;
			default:
				inv = ""; break;
		}

		/*
			XXX we need a better way to find out what direction
			the pins is
		 */
		switch(*cp){
			case 'Q':
			case 'O':
				dir = 'O'; break;
			default:
				dir = 'I'; break;
		}

		net = NodeAlias(nl,ob);

		if(!strcmp(net,"Gnd"))
			net = gndnet;

		if(!strcmp(net,"Vcc"))
			net = vccnet;

		if(!strcmp(net,"Vdd"))
			net = vccnet;
		FlushString("PIN,%s,%c,%s,,%s\n",
			cp,
			dir,
			xilinx_name("n$",net),
			inv
		);
		ob = ob->next;
	}

	if(!strncmp(gob->model.class,"DF1",3)){
		FlushString("PIN,RD,I,%s\n",xilinx_name("n$",gndnet));
		FlushString("PIN,CE,I,%s\n",xilinx_name("n$",vccnet));
	}

	if(!strncmp(gob->model.class,"DFC1",4)){
		FlushString("PIN,CE,I,%s\n",xilinx_name("n$",vccnet));
	}

	if(!strncmp(gob->model.class,"DFE",3)){
		FlushString("PIN,RD,I,%s\n",xilinx_name("n$",gndnet));
	}

	FlushString("END\n");

	ob = gob;
	pin = 0;
	while(ob){
		if(ob->type <= pin)
			break;
		pin = ob->type;
		net = NodeAlias(nl,ob);
		cp  = rindex(ob->name,'/');
		xx = LookupObject(net,nl);
		if(xx){
			cp++;
			switch(*cp){
				case '!':
					inv = "INV"; cp++; break;
				default:
					inv = ""; break;
			}
			switch(*cp){
				case 'Q':
				case 'O':
					dir = 'O'; break;
				default:
					dir = 'I'; break;
			}
			if(IsPort(xx)){
				if(dir == 'O')
					xx->type = -92;
				if(dir == 'I')
					xx->type = -91;
			}
			if(xx->type == -92 && dir == 'I')
				xx->type = -93;
			if(xx->type == -91 && dir == 'O')
				xx->type = -93;
		}
		ob = ob->next;
	}
}
