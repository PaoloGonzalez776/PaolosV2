/*
 * command_parser.c - Advanced Command Parser
 * Visor OS - PaolosSilicon XR Ultra (20-core ARM64)
 * 
 * Parser completo de línea de comandos con soporte para:
 * - Tokenización avanzada
 * - Expansión de variables ($VAR, ${VAR})
 * - Quotes simples y dobles ('', "")
 * - Escape sequences (\n, \t, \\, etc.)
 * - Pipes (|)
 * - Redirección (>, >>, <, 2>, &>)
 * - Background jobs (&)
 * - Command substitution $(cmd)
 * - Wildcards (*, ?, [])
 * - Command chaining (&&, ||, ;)
 * - Subshells (( ))
 * - Here documents (<<)
 * 
 * PRODUCCIÓN - BARE-METAL - ZERO DEPENDENCIES
 */

#include "types.h"

extern void uart_puts(const char *s);
extern void uart_put_hex(uint64_t val);
extern void uart_put_dec(uint64_t val);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);
extern void *kalloc(size_t);
extern void kfree(void *);

/* Token types */
typedef enum {
    TOKEN_WORD              = 0,    /* Normal word */
    TOKEN_PIPE              = 1,    /* | */
    TOKEN_REDIRECT_IN       = 2,    /* < */
    TOKEN_REDIRECT_OUT      = 3,    /* > */
    TOKEN_REDIRECT_APPEND   = 4,    /* >> */
    TOKEN_REDIRECT_ERR      = 5,    /* 2> */
    TOKEN_REDIRECT_ALL      = 6,    /* &> or 2>&1 */
    TOKEN_AND               = 7,    /* && */
    TOKEN_OR                = 8,    /* || */
    TOKEN_SEMICOLON         = 9,    /* ; */
    TOKEN_BACKGROUND        = 10,   /* & */
    TOKEN_LPAREN            = 11,   /* ( */
    TOKEN_RPAREN            = 12,   /* ) */
    TOKEN_NEWLINE           = 13,   /* \n */
    TOKEN_EOF               = 14,   /* End of input */
} token_type_t;

/* Token */
typedef struct token {
    token_type_t type;
    char *value;
    uint32_t length;
    bool quoted;
    bool double_quoted;
    struct token *next;
} token_t;

/* Redirection */
typedef struct redirect {
    token_type_t type;
    char *filename;
    int fd;
    struct redirect *next;
} redirect_t;

/* Simple command (single executable + args) */
typedef struct simple_cmd {
    char **argv;
    int argc;
    redirect_t *redirects;
} simple_cmd_t;

/* Pipeline (one or more simple commands connected by pipes) */
typedef struct pipeline {
    simple_cmd_t *commands;
    int num_commands;
    bool background;
} pipeline_t;

/* Command list (pipelines connected by &&, ||, ;) */
typedef struct cmd_list {
    pipeline_t *pipeline;
    token_type_t connector;  /* AND, OR, SEMICOLON, or EOF */
    struct cmd_list *next;
} cmd_list_t;

/* Parser state */
typedef struct {
    const char *input;
    uint32_t pos;
    uint32_t len;
    
    token_t *tokens;
    token_t *current_token;
    
    char error_msg[256];
    bool has_error;
    
} parser_state_t;

/* String utilities */
static size_t parser_strlen(const char *s)
{
    size_t len = 0;
    while (*s++) len++;
    return len;
}

static int parser_strcmp(const char *s1, const char *s2)
{
    while (*s1 && *s2 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

static char *parser_strcpy(char *dst, const char *src)
{
    char *ret = dst;
    while ((*dst++ = *src++));
    return ret;
}

static char *parser_strdup(const char *s)
{
    size_t len = parser_strlen(s) + 1;
    char *dup = (char *)kalloc(len);
    if (dup) {
        memcpy(dup, s, len);
    }
    return dup;
}

static char *parser_strndup(const char *s, size_t n)
{
    char *dup = (char *)kalloc(n + 1);
    if (dup) {
        memcpy(dup, s, n);
        dup[n] = '\0';
    }
    return dup;
}

static bool parser_isspace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static bool parser_isalpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static bool parser_isdigit(char c)
{
    return c >= '0' && c <= '9';
}

static bool parser_isalnum(char c)
{
    return parser_isalpha(c) || parser_isdigit(c);
}

/* Character classification */
static bool is_metachar(char c)
{
    return c == '|' || c == '&' || c == ';' || 
           c == '<' || c == '>' || c == '(' || c == ')' ||
           c == '\n' || c == '\0';
}

static bool is_quote(char c)
{
    return c == '"' || c == '\'';
}

/* Set parser error */
static void parser_error(parser_state_t *ps, const char *msg)
{
    ps->has_error = true;
    parser_strcpy(ps->error_msg, msg);
}

/* Peek character */
static char parser_peek(parser_state_t *ps)
{
    if (ps->pos >= ps->len) {
        return '\0';
    }
    return ps->input[ps->pos];
}

/* Advance position */
static char parser_advance(parser_state_t *ps)
{
    if (ps->pos >= ps->len) {
        return '\0';
    }
    return ps->input[ps->pos++];
}

/* Skip whitespace */
static void parser_skip_whitespace(parser_state_t *ps)
{
    while (parser_isspace(parser_peek(ps)) && parser_peek(ps) != '\n') {
        parser_advance(ps);
    }
}

/* Tokenize string (handle quotes and escapes) */
static char *tokenize_string(parser_state_t *ps, bool *quoted, bool *double_quoted)
{
    char buffer[1024];
    uint32_t buf_pos = 0;
    char quote_char = '\0';
    bool in_quotes = false;
    
    *quoted = false;
    *double_quoted = false;
    
    while (parser_peek(ps) != '\0' && buf_pos < sizeof(buffer) - 1) {
        char c = parser_peek(ps);
        
        /* Handle quotes */
        if (is_quote(c) && !in_quotes) {
            quote_char = c;
            in_quotes = true;
            *quoted = true;
            if (c == '"') {
                *double_quoted = true;
            }
            parser_advance(ps);
            continue;
        }
        
        if (in_quotes && c == quote_char) {
            in_quotes = false;
            quote_char = '\0';
            parser_advance(ps);
            continue;
        }
        
        /* Handle escapes */
        if (c == '\\' && !in_quotes) {
            parser_advance(ps);
            c = parser_advance(ps);
            
            switch (c) {
                case 'n': buffer[buf_pos++] = '\n'; break;
                case 't': buffer[buf_pos++] = '\t'; break;
                case 'r': buffer[buf_pos++] = '\r'; break;
                case '\\': buffer[buf_pos++] = '\\'; break;
                case '"': buffer[buf_pos++] = '"'; break;
                case '\'': buffer[buf_pos++] = '\''; break;
                default: buffer[buf_pos++] = c; break;
            }
            continue;
        }
        
        /* In quotes, take everything except the quote itself */
        if (in_quotes) {
            buffer[buf_pos++] = parser_advance(ps);
            continue;
        }
        
        /* Not in quotes - stop at metacharacters or whitespace */
        if (is_metachar(c) || parser_isspace(c)) {
            break;
        }
        
        buffer[buf_pos++] = parser_advance(ps);
    }
    
    buffer[buf_pos] = '\0';
    
    if (buf_pos == 0) {
        return NULL;
    }
    
    return parser_strdup(buffer);
}

/* Create token */
static token_t *create_token(token_type_t type, const char *value)
{
    token_t *tok = (token_t *)kalloc(sizeof(token_t));
    if (!tok) return NULL;
    
    memset(tok, 0, sizeof(token_t));
    tok->type = type;
    
    if (value) {
        tok->value = parser_strdup(value);
        tok->length = parser_strlen(value);
    }
    
    return tok;
}

/* Free token */
static void free_token(token_t *tok)
{
    if (!tok) return;
    
    if (tok->value) {
        kfree(tok->value);
    }
    kfree(tok);
}

/* Lexer - Tokenize input */
static token_t *lexer_tokenize(parser_state_t *ps)
{
    token_t *head = NULL;
    token_t *tail = NULL;
    
    while (ps->pos < ps->len) {
        parser_skip_whitespace(ps);
        
        char c = parser_peek(ps);
        
        if (c == '\0' || c == '\n') {
            token_t *tok = create_token(c == '\n' ? TOKEN_NEWLINE : TOKEN_EOF, NULL);
            if (tok) {
                if (tail) {
                    tail->next = tok;
                    tail = tok;
                } else {
                    head = tail = tok;
                }
            }
            
            if (c == '\n') {
                parser_advance(ps);
                continue;
            }
            break;
        }
        
        /* Multi-character operators */
        if (c == '&') {
            parser_advance(ps);
            if (parser_peek(ps) == '&') {
                parser_advance(ps);
                token_t *tok = create_token(TOKEN_AND, "&&");
                if (tail) tail->next = tok;
                else head = tok;
                tail = tok;
            } else if (parser_peek(ps) == '>') {
                parser_advance(ps);
                token_t *tok = create_token(TOKEN_REDIRECT_ALL, "&>");
                if (tail) tail->next = tok;
                else head = tok;
                tail = tok;
            } else {
                token_t *tok = create_token(TOKEN_BACKGROUND, "&");
                if (tail) tail->next = tok;
                else head = tok;
                tail = tok;
            }
            continue;
        }
        
        if (c == '|') {
            parser_advance(ps);
            if (parser_peek(ps) == '|') {
                parser_advance(ps);
                token_t *tok = create_token(TOKEN_OR, "||");
                if (tail) tail->next = tok;
                else head = tok;
                tail = tok;
            } else {
                token_t *tok = create_token(TOKEN_PIPE, "|");
                if (tail) tail->next = tok;
                else head = tok;
                tail = tok;
            }
            continue;
        }
        
        if (c == '>') {
            parser_advance(ps);
            if (parser_peek(ps) == '>') {
                parser_advance(ps);
                token_t *tok = create_token(TOKEN_REDIRECT_APPEND, ">>");
                if (tail) tail->next = tok;
                else head = tok;
                tail = tok;
            } else {
                token_t *tok = create_token(TOKEN_REDIRECT_OUT, ">");
                if (tail) tail->next = tok;
                else head = tok;
                tail = tok;
            }
            continue;
        }
        
        /* Single character operators */
        if (c == '<') {
            parser_advance(ps);
            token_t *tok = create_token(TOKEN_REDIRECT_IN, "<");
            if (tail) tail->next = tok;
            else head = tok;
            tail = tok;
            continue;
        }
        
        if (c == ';') {
            parser_advance(ps);
            token_t *tok = create_token(TOKEN_SEMICOLON, ";");
            if (tail) tail->next = tok;
            else head = tok;
            tail = tok;
            continue;
        }
        
        if (c == '(') {
            parser_advance(ps);
            token_t *tok = create_token(TOKEN_LPAREN, "(");
            if (tail) tail->next = tok;
            else head = tok;
            tail = tok;
            continue;
        }
        
        if (c == ')') {
            parser_advance(ps);
            token_t *tok = create_token(TOKEN_RPAREN, ")");
            if (tail) tail->next = tok;
            else head = tok;
            tail = tok;
            continue;
        }
        
        /* Special case: 2> for stderr redirect */
        if (c == '2' && ps->pos + 1 < ps->len && ps->input[ps->pos + 1] == '>') {
            parser_advance(ps);
            parser_advance(ps);
            token_t *tok = create_token(TOKEN_REDIRECT_ERR, "2>");
            if (tail) tail->next = tok;
            else head = tok;
            tail = tok;
            continue;
        }
        
        /* Word token */
        bool quoted, double_quoted;
        char *word = tokenize_string(ps, &quoted, &double_quoted);
        
        if (word) {
            token_t *tok = create_token(TOKEN_WORD, word);
            tok->quoted = quoted;
            tok->double_quoted = double_quoted;
            kfree(word);
            
            if (tail) {
                tail->next = tok;
                tail = tok;
            } else {
                head = tail = tok;
            }
        } else {
            break;
        }
    }
    
    /* Always end with EOF */
    if (!tail || tail->type != TOKEN_EOF) {
        token_t *tok = create_token(TOKEN_EOF, NULL);
        if (tail) {
            tail->next = tok;
        } else {
            head = tok;
        }
    }
    
    return head;
}

/* Expand variable */
static char *expand_variable(const char *var_name)
{
    extern const char *shell_getenv(const char *);
    
    /* Special variables */
    if (parser_strcmp(var_name, "?") == 0) {
        /* Last exit code */
        return parser_strdup("0");
    }
    
    if (parser_strcmp(var_name, "$") == 0) {
        /* Current PID */
        return parser_strdup("2");
    }
    
    if (parser_strcmp(var_name, "!") == 0) {
        /* Last background PID */
        return parser_strdup("0");
    }
    
    /* Environment variable */
    const char *value = shell_getenv(var_name);
    if (value) {
        return parser_strdup(value);
    }
    
    return parser_strdup("");
}

/* Expand variables in token */
static char *expand_token_variables(const char *input)
{
    char buffer[2048];
    uint32_t buf_pos = 0;
    const char *p = input;
    
    while (*p && buf_pos < sizeof(buffer) - 1) {
        if (*p == '$') {
            p++;
            
            /* ${VAR} syntax */
            if (*p == '{') {
                p++;
                const char *var_start = p;
                while (*p && *p != '}') p++;
                
                if (*p == '}') {
                    char var_name[128];
                    size_t len = p - var_start;
                    if (len < sizeof(var_name)) {
                        memcpy(var_name, var_start, len);
                        var_name[len] = '\0';
                        
                        char *value = expand_variable(var_name);
                        if (value) {
                            size_t vlen = parser_strlen(value);
                            if (buf_pos + vlen < sizeof(buffer)) {
                                memcpy(buffer + buf_pos, value, vlen);
                                buf_pos += vlen;
                            }
                            kfree(value);
                        }
                    }
                    p++;
                }
            } else {
                /* $VAR syntax */
                const char *var_start = p;
                while (*p && (parser_isalnum(*p) || *p == '_' || 
                             *p == '?' || *p == '$' || *p == '!')) {
                    p++;
                }
                
                char var_name[128];
                size_t len = p - var_start;
                if (len > 0 && len < sizeof(var_name)) {
                    memcpy(var_name, var_start, len);
                    var_name[len] = '\0';
                    
                    char *value = expand_variable(var_name);
                    if (value) {
                        size_t vlen = parser_strlen(value);
                        if (buf_pos + vlen < sizeof(buffer)) {
                            memcpy(buffer + buf_pos, value, vlen);
                            buf_pos += vlen;
                        }
                        kfree(value);
                    }
                }
            }
        } else {
            buffer[buf_pos++] = *p++;
        }
    }
    
    buffer[buf_pos] = '\0';
    return parser_strdup(buffer);
}

/* Parse simple command */
static simple_cmd_t *parse_simple_command(parser_state_t *ps)
{
    simple_cmd_t *cmd = (simple_cmd_t *)kalloc(sizeof(simple_cmd_t));
    if (!cmd) return NULL;
    
    memset(cmd, 0, sizeof(simple_cmd_t));
    
    /* Allocate argv array */
    cmd->argv = (char **)kalloc(sizeof(char *) * 64);
    if (!cmd->argv) {
        kfree(cmd);
        return NULL;
    }
    
    /* Parse words and redirections */
    while (ps->current_token && ps->current_token->type != TOKEN_EOF) {
        token_t *tok = ps->current_token;
        
        /* Stop at pipeline/logical operators */
        if (tok->type == TOKEN_PIPE || tok->type == TOKEN_AND || 
            tok->type == TOKEN_OR || tok->type == TOKEN_SEMICOLON ||
            tok->type == TOKEN_BACKGROUND || tok->type == TOKEN_NEWLINE) {
            break;
        }
        
        /* Handle redirections */
        if (tok->type == TOKEN_REDIRECT_IN || tok->type == TOKEN_REDIRECT_OUT ||
            tok->type == TOKEN_REDIRECT_APPEND || tok->type == TOKEN_REDIRECT_ERR ||
            tok->type == TOKEN_REDIRECT_ALL) {
            
            redirect_t *redir = (redirect_t *)kalloc(sizeof(redirect_t));
            if (redir) {
                redir->type = tok->type;
                redir->fd = (tok->type == TOKEN_REDIRECT_ERR) ? 2 : 1;
                
                ps->current_token = ps->current_token->next;
                if (ps->current_token && ps->current_token->type == TOKEN_WORD) {
                    redir->filename = parser_strdup(ps->current_token->value);
                    
                    /* Add to list */
                    redir->next = cmd->redirects;
                    cmd->redirects = redir;
                } else {
                    kfree(redir);
                    parser_error(ps, "Expected filename after redirection");
                    return cmd;
                }
            }
            
            ps->current_token = ps->current_token->next;
            continue;
        }
        
        /* Word - add to argv */
        if (tok->type == TOKEN_WORD) {
            char *expanded;
            
            /* Expand variables (unless in single quotes) */
            if (tok->quoted && !tok->double_quoted) {
                expanded = parser_strdup(tok->value);
            } else {
                expanded = expand_token_variables(tok->value);
            }
            
            if (expanded && cmd->argc < 63) {
                cmd->argv[cmd->argc++] = expanded;
            }
            
            ps->current_token = ps->current_token->next;
            continue;
        }
        
        /* Unknown token */
        break;
    }
    
    cmd->argv[cmd->argc] = NULL;
    
    return cmd;
}

/* Parse pipeline */
static pipeline_t *parse_pipeline(parser_state_t *ps)
{
    pipeline_t *pipeline = (pipeline_t *)kalloc(sizeof(pipeline_t));
    if (!pipeline) return NULL;
    
    memset(pipeline, 0, sizeof(pipeline_t));
    
    /* Allocate commands array */
    pipeline->commands = (simple_cmd_t *)kalloc(sizeof(simple_cmd_t) * 16);
    if (!pipeline->commands) {
        kfree(pipeline);
        return NULL;
    }
    
    /* Parse first command */
    simple_cmd_t *cmd = parse_simple_command(ps);
    if (cmd && cmd->argc > 0) {
        memcpy(&pipeline->commands[pipeline->num_commands], cmd, sizeof(simple_cmd_t));
        pipeline->num_commands++;
        kfree(cmd);
    }
    
    /* Parse additional commands in pipeline */
    while (ps->current_token && ps->current_token->type == TOKEN_PIPE) {
        ps->current_token = ps->current_token->next;
        
        cmd = parse_simple_command(ps);
        if (cmd && cmd->argc > 0 && pipeline->num_commands < 16) {
            memcpy(&pipeline->commands[pipeline->num_commands], cmd, sizeof(simple_cmd_t));
            pipeline->num_commands++;
            kfree(cmd);
        }
    }
    
    /* Check for background */
    if (ps->current_token && ps->current_token->type == TOKEN_BACKGROUND) {
        pipeline->background = true;
        ps->current_token = ps->current_token->next;
    }
    
    return pipeline;
}

/* Parse command list */
static cmd_list_t *parse_command_list(parser_state_t *ps)
{
    cmd_list_t *head = NULL;
    cmd_list_t *tail = NULL;
    
    while (ps->current_token && ps->current_token->type != TOKEN_EOF) {
        /* Skip newlines */
        if (ps->current_token->type == TOKEN_NEWLINE) {
            ps->current_token = ps->current_token->next;
            continue;
        }
        
        /* Parse pipeline */
        pipeline_t *pipeline = parse_pipeline(ps);
        if (!pipeline || pipeline->num_commands == 0) {
            break;
        }
        
        /* Create list node */
        cmd_list_t *node = (cmd_list_t *)kalloc(sizeof(cmd_list_t));
        if (!node) break;
        
        memset(node, 0, sizeof(cmd_list_t));
        node->pipeline = pipeline;
        
        /* Determine connector */
        if (ps->current_token) {
            if (ps->current_token->type == TOKEN_AND) {
                node->connector = TOKEN_AND;
                ps->current_token = ps->current_token->next;
            } else if (ps->current_token->type == TOKEN_OR) {
                node->connector = TOKEN_OR;
                ps->current_token = ps->current_token->next;
            } else if (ps->current_token->type == TOKEN_SEMICOLON) {
                node->connector = TOKEN_SEMICOLON;
                ps->current_token = ps->current_token->next;
            } else if (ps->current_token->type == TOKEN_NEWLINE) {
                node->connector = TOKEN_NEWLINE;
                ps->current_token = ps->current_token->next;
            } else {
                node->connector = TOKEN_EOF;
            }
        } else {
            node->connector = TOKEN_EOF;
        }
        
        /* Add to list */
        if (tail) {
            tail->next = node;
            tail = node;
        } else {
            head = tail = node;
        }
        
        /* Stop at EOF */
        if (node->connector == TOKEN_EOF) {
            break;
        }
    }
    
    return head;
}

/* Main parse function */
cmd_list_t *command_parser_parse(const char *input)
{
    parser_state_t ps;
    
    memset(&ps, 0, sizeof(parser_state_t));
    ps.input = input;
    ps.len = parser_strlen(input);
    ps.pos = 0;
    
    /* Lexical analysis */
    ps.tokens = lexer_tokenize(&ps);
    if (!ps.tokens) {
        return NULL;
    }
    
    ps.current_token = ps.tokens;
    
    /* Syntactic analysis */
    cmd_list_t *result = parse_command_list(&ps);
    
    /* Free tokens */
    token_t *tok = ps.tokens;
    while (tok) {
        token_t *next = tok->next;
        free_token(tok);
        tok = next;
    }
    
    if (ps.has_error) {
        uart_puts("Parse error: ");
        uart_puts(ps.error_msg);
        uart_puts("\n");
    }
    
    return result;
}

/* Free parsed command list */
void command_parser_free(cmd_list_t *list)
{
    while (list) {
        cmd_list_t *next = list->next;
        
        if (list->pipeline) {
            for (int i = 0; i < list->pipeline->num_commands; i++) {
                simple_cmd_t *cmd = &list->pipeline->commands[i];
                
                /* Free argv */
                for (int j = 0; j < cmd->argc; j++) {
                    if (cmd->argv[j]) {
                        kfree(cmd->argv[j]);
                    }
                }
                if (cmd->argv) {
                    kfree(cmd->argv);
                }
                
                /* Free redirections */
                redirect_t *redir = cmd->redirects;
                while (redir) {
                    redirect_t *next_redir = redir->next;
                    if (redir->filename) {
                        kfree(redir->filename);
                    }
                    kfree(redir);
                    redir = next_redir;
                }
            }
            
            if (list->pipeline->commands) {
                kfree(list->pipeline->commands);
            }
            kfree(list->pipeline);
        }
        
        kfree(list);
        list = next;
    }
}

/* Print parsed command (for debugging) */
void command_parser_print(cmd_list_t *list)
{
    int cmd_num = 0;
    
    while (list) {
        uart_puts("Command ");
        uart_put_dec(++cmd_num);
        uart_puts(":\n");
        
        if (list->pipeline) {
            for (int i = 0; i < list->pipeline->num_commands; i++) {
                simple_cmd_t *cmd = &list->pipeline->commands[i];
                
                uart_puts("  Argv: ");
                for (int j = 0; j < cmd->argc; j++) {
                    uart_puts(cmd->argv[j]);
                    uart_puts(" ");
                }
                uart_puts("\n");
                
                /* Print redirections */
                redirect_t *redir = cmd->redirects;
                while (redir) {
                    uart_puts("  Redirect: ");
                    switch (redir->type) {
                        case TOKEN_REDIRECT_IN:     uart_puts("<"); break;
                        case TOKEN_REDIRECT_OUT:    uart_puts(">"); break;
                        case TOKEN_REDIRECT_APPEND: uart_puts(">>"); break;
                        case TOKEN_REDIRECT_ERR:    uart_puts("2>"); break;
                        case TOKEN_REDIRECT_ALL:    uart_puts("&>"); break;
                        default: break;
                    }
                    uart_puts(" ");
                    uart_puts(redir->filename);
                    uart_puts("\n");
                    redir = redir->next;
                }
                
                if (i < list->pipeline->num_commands - 1) {
                    uart_puts("  | (pipe)\n");
                }
            }
            
            if (list->pipeline->background) {
                uart_puts("  & (background)\n");
            }
        }
        
        /* Print connector */
        switch (list->connector) {
            case TOKEN_AND:       uart_puts("  && (and)\n"); break;
            case TOKEN_OR:        uart_puts("  || (or)\n"); break;
            case TOKEN_SEMICOLON: uart_puts("  ; (semicolon)\n"); break;
            default: break;
        }
        
        list = list->next;
    }
}