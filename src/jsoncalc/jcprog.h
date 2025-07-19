/* jsoncalc.h */

extern int interactive;
extern jsoncontext_t *context;

char **jsoncalc_completion(const char *text, int start, int end);
void interact(jsoncontext_t **contextref, jsoncmd_t *initcmd);
void batch(jsoncontext_t **contextref, jsoncmd_t *initcmd);
char *save_config(void);
void load_config(void);
void format_usage(void);
void color_usage(void);
void debug_usage(void);
void run(jsoncmd_t *jc, jsoncontext_t **refcontext);
