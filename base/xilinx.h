#ifndef _XILINX_H
#define _XILINX_H

extern void xilinx_sym(struct nlist *nl, struct objlist *gob);
extern void Xilinx(char *cellname, char *filename);
extern int xilinxCell(char *cell);
extern int XilinxLibPresent(void);
extern void XilinxLib(void);

#endif /* _XILINX_H */
