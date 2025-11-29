/* jxprog.h */

extern int interactive;
extern jxcontext_t *context;

char **jx_completion(const char *text, int start, int end);
void interact(jxcontext_t **contextref, jxcmd_t *initcmd);
void batch(jxcontext_t **contextref, jxcmd_t *initcmd);
char *save_config(void);
void load_config(void);
void format_usage(void);
void color_usage(void);
void debug_usage(void);
void run(jxcmd_t *jc, jxcontext_t **refcontext);
