#ifdef __STDC__
void X_display_line(char *buf);
void X_display_cell(char *buf);
void X_display_refresh(void);
void X_clear_display(void);
void X_clear_cell(void);

void X_main_loop(int argc, char *argv[]);  /* also in netgen.h */
#else

void X_display_line();
void X_display_cell();
void X_display_refresh();
void X_clear_display();
void X_clear_cell();

void X_main_loop();  /* also in netgen.h */
#endif
