#ifndef _XILINX_H
#define _XILINX_H

void xilinx_sym(struct nlist *nl, struct objlist *gob);
void Xilinx(char *cellname, char *filename);
int xilinxCell(char *cell);
int XilinxLibPresent(void);
void XilinxLib(void);

#endif /* _XILINX_H */