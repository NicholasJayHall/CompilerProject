#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "codegen.h"
#include "tree.h"
#include "semantics.h"

static int g_counter = 0;
static int next_id() { return g_counter++; }

int emit_node(Node* node, FILE* out, int id, Symbol* sym_table);

static int codepoint_to_utf8(int cp, char* buf) {
    if (cp <= 0x7F) {
        buf[0] = (char)cp;
        return 1;
    } else if (cp <= 0x7FF) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else if (cp <= 0xFFFF) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    } else {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
}

static void emit_literal_string(const char* raw, FILE* out) {
    /* raw includes the surrounding double-quotes from the lexer */
    const char* p = raw + 1; /* skip opening " */
    fputc('"', out);
    while (*p && !(*p == '"' && *(p+1) == '\0') && *p != '\0') {
        /* check for end quote */
        if (*p == '"') { break; }

        if (*p == '%' && *(p+1) == 'x') {
            p += 2; /* skip %x */
            int cp = 0;
            while (*p >= '0' && *p <= '9') {
                cp = cp * 10 + (*p - '0');
                p++;
            }
            if (*p == ';') p++; /* skip ; */
            char utf8[4];
            int nbytes = codepoint_to_utf8(cp, utf8);
            for (int i = 0; i < nbytes; i++) {
                fprintf(out, "\\x%02x", (unsigned char)utf8[i]);
            }
        } else if (*p == '\\') {
            /* pass through existing escape sequences */
            fputc(*p, out);
            p++;
            if (*p) { fputc(*p, out); p++; }
        } else {
            /* ordinary character -- escape anything that would break a C string */
            if (*p == '"') {
                fputs("\\\"", out);
            } else if (*p == '\\') {
                fputs("\\\\", out);
            } else {
                fputc(*p, out);
            }
            p++;
        }
    }
    fputc('"', out);
}

static int literal_byte_length(const char* raw) {
    const char* p = raw + 1; /* skip opening " */
    int len = 0;
    while (*p && *p != '"') {
        if (*p == '%' && *(p+1) == 'x') {
            p += 2;
            int cp = 0;
            while (*p >= '0' && *p <= '9') { cp = cp * 10 + (*p - '0'); p++; }
            if (*p == ';') p++;
            /* count UTF-8 bytes */
            if      (cp <= 0x7F)   len += 1;
            else if (cp <= 0x7FF)  len += 2;
            else if (cp <= 0xFFFF) len += 3;
            else                   len += 4;
        } else {
            len++;
            p++;
        }
    }
    return len;
}

static void emit_range_char(const char* raw, FILE* out) {
    if (raw[0] == '%' && raw[1] == 'x') {
        const char* p = raw + 2;
        int cp = 0;
        while (*p >= '0' && *p <= '9') { cp = cp * 10 + (*p - '0'); p++; }
        /* For simplicity emit the first byte of the UTF-8 encoding.
           Full multi-byte range support would require a more complex
           generated runtime. */
        char utf8[4];
        codepoint_to_utf8(cp, utf8);
        fprintf(out, "'\\x%02x'", (unsigned char)utf8[0]);
    } else if (raw[0] == '\'') {
        fprintf(out, "\\'");
    } else if (raw[0] == '\\') {
        fprintf(out, "'\\\\'");
    } else {
        fprintf(out, "'%c'", raw[0]);
    }
}

/* -----------------------------------------------------------------------
 * emit_node
 *
 * Recursively walks the AST.  For each node it:
 *   1. Recurses into children FIRST (post-order) so that child functions
 *      are defined before the parent that calls them.
 *   2. Emits a C function  int match_<id>(const char*,int,int)  that
 *      implements the matching logic for this node by calling the child
 *      functions.
 *
 * Returns the `id` of the function it emitted so the caller can call it.
 * ----------------------------------------------------------------------- */
int emit_node(Node* node, FILE* out, int id, Symbol* sym_table) {
    if (node == NULL) return -1;

    switch (node->type) {

        /* ---- PROGRAM root: just emit the main regex (right child) ---- */
        case NODE_PROGRAM: {
            /* left = definitions chain, right = root regex */
            /* Definitions are handled via the symbol table substitution
               in NODE_SUB, so we only need to emit the root regex. */
            int root_id = next_id();
            emit_node(node->right, out, root_id, sym_table);
            return root_id;
        }

        /* ---- LITERAL: strncmp at current position ---- */
        case NODE_LITERAL: {
            int byte_len = literal_byte_length(node->value);
            fprintf(out,
                "/* LITERAL */\n"
                "static int match_%d(const char* s, int pos, int len) {\n"
                "    const char* lit = ", id);
            emit_literal_string(node->value, out);
            fprintf(out, ";\n"
                "    int litlen = %d;\n"
                "    if (pos + litlen > len) return -1;\n"
                "    if (memcmp(s + pos, lit, litlen) == 0) return pos + litlen;\n"
                "    return -1;\n"
                "}\n\n", byte_len);
            return id;
        }

        /* ---- WILD: match any single byte ---- */
        case NODE_WILD: {
            fprintf(out,
                "/* WILD */\n"
                "static int match_%d(const char* s, int pos, int len) {\n"
                "    if (pos < len) return pos + 1;\n"
                "    return -1;\n"
                "}\n\n", id);
            return id;
        }

        /* ---- SEQ: match left then right ---- */
        case NODE_SEQ: {
            int left_id  = next_id();
            int right_id = next_id();
            emit_node(node->left,  out, left_id,  sym_table);
            emit_node(node->right, out, right_id, sym_table);
            fprintf(out,
                "/* SEQ */\n"
                "static int match_%d(const char* s, int pos, int len) {\n"
                "    int mid = match_%d(s, pos, len);\n"
                "    if (mid == -1) return -1;\n"
                "    return match_%d(s, mid, len);\n"
                "}\n\n", id, left_id, right_id);
            return id;
        }

        /* ---- ALT: try left, fall back to right ---- */
        case NODE_ALT: {
            if (node->right == NULL) {
                /* This is the negation form from the parser: EXCLAM Regex
                   or a negated character class [^ ... ] */
                int child_id = next_id();
                emit_node(node->left, out, child_id, sym_table);
                fprintf(out,
                    "/* NEGATION */\n"
                    "static int match_%d(const char* s, int pos, int len) {\n"
                    "    int r = match_%d(s, pos, len);\n"
                    "    /* negation: succeed (consuming 1 char) only if child fails */\n"
                    "    if (r == -1 && pos < len) return pos + 1;\n"
                    "    return -1;\n"
                    "}\n\n", id, child_id);
            } else {
                int left_id  = next_id();
                int right_id = next_id();
                emit_node(node->left,  out, left_id,  sym_table);
                emit_node(node->right, out, right_id, sym_table);
                fprintf(out,
                    "/* ALT */\n"
                    "static int match_%d(const char* s, int pos, int len) {\n"
                    "    int r = match_%d(s, pos, len);\n"
                    "    if (r != -1) return r;\n"
                    "    return match_%d(s, pos, len);\n"
                    "}\n\n", id, left_id, right_id);
            }
            return id;
        }

        /* ---- REPEAT: *, +, ? ---- */
        case NODE_REPEAT: {
            int child_id = next_id();
            emit_node(node->left, out, child_id, sym_table);

            if (node->op == '+') {
                fprintf(out,
               	"	/* REPEAT + */\n"
    		"static int match_%d(const char* s, int pos, int len) {\n"
    		"    	int cur = match_%d(s, pos, len);\n"
    		"    	if (cur == -1) return -1;\n"
    		"    	int next;\n"
    		"   	while ((next = match_%d(s, cur, len)) != -1 && next > cur) cur = next;\n"
    		"	return cur;\n"
   		"}\n\n", id, child_id, child_id);
            } else if (node->op == '*') {
               fprintf(out,
    		"/* REPEAT * */\n"
    		"static int match_%d(const char* s, int pos, int len) {\n"
    		"    int cur = pos, next;\n"
    		"    while ((next = match_%d(s, cur, len)) != -1 && next > cur) cur = next;\n"
   		 "    return cur;\n"
    		"}\n\n", id, child_id); 
            } else { /* '?' */
                fprintf(out,
                    "/* REPEAT ? */\n"
                    "static int match_%d(const char* s, int pos, int len) {\n"
                    "    int r = match_%d(s, pos, len);\n"
                    "    return (r != -1) ? r : pos; /* zero or one */\n"
                    "}\n\n", id, child_id);
            }
            return id;
        }

        /* ---- RANGE: character class ---- */
        case NODE_RANGE: {
            /*
             * The parser builds ranges as a binary tree of NODE_RANGE nodes.
             * A leaf NODE_RANGE has value = single char (start) and
             * right = leaf with value = end char for "a-z" style ranges.
             * A non-leaf NODE_RANGE has left and right subtrees to combine.
             */
            if (node->left != NULL && node->right != NULL &&
                node->left->type == NODE_RANGE && node->right->type != NODE_RANGE) {
                /* This is a  X-Y  range item: value=X, right->value=Y */
                fprintf(out,
                    "/* RANGE item X-Y */\n"
                    "static int match_%d(const char* s, int pos, int len) {\n"
                    "    if (pos >= len) return -1;\n"
                    "    unsigned char c = (unsigned char)s[pos];\n"
                    "    if (c >= ", id);
                emit_range_char(node->value, out);
                fprintf(out, " && c <= ");
                emit_range_char(node->right->value, out);
                fprintf(out, ") return pos + 1;\n"
                    "    return -1;\n"
                    "}\n\n");
            } else if (node->left != NULL && node->right != NULL) {
                /* Combined range: try left then right (union) */
                int left_id  = next_id();
                int right_id = next_id();
                emit_node(node->left,  out, left_id,  sym_table);
                emit_node(node->right, out, right_id, sym_table);
                fprintf(out,
                    "/* RANGE union */\n"
                    "static int match_%d(const char* s, int pos, int len) {\n"
                    "    int r = match_%d(s, pos, len);\n"
                    "    if (r != -1) return r;\n"
                    "    return match_%d(s, pos, len);\n"
                    "}\n\n", id, left_id, right_id);
            } else {
                /* Single character range item */
                fprintf(out,
                    "/* RANGE single char */\n"
                    "static int match_%d(const char* s, int pos, int len) {\n"
                    "    if (pos >= len) return -1;\n"
                    "    unsigned char c = (unsigned char)s[pos];\n"
                    "    if (c == ", id);
                emit_range_char(node->value, out);
                fprintf(out, ") return pos + 1;\n"
                    "    return -1;\n"
                    "}\n\n");
            }
            return id;
        }

        /* ---- NODE_ID inside a range (single printable char) ---- */
        case NODE_ID: {
            /* Inside a character class a bare ID is a single character token */
            fprintf(out,
                "/* RANGE ID char */\n"
                "static int match_%d(const char* s, int pos, int len) {\n"
                "    if (pos >= len) return -1;\n"
                "    /* match any character in the identifier string */\n"
                "    const char* chars = \"%s\";\n"
                "    int clen = (int)strlen(chars);\n"
                "    for (int i = 0; i < clen; i++) {\n"
                "        if (s[pos] == chars[i]) return pos + 1;\n"
                "    }\n"
                "    return -1;\n"
                "}\n\n", id, node->value);
            return id;
        }

        /* ---- SUBSTITUTE: inline the referred constant's subtree ---- */
        case NODE_SUB: {
            /* Look up the symbol in the symbol table and emit its subtree */
            Symbol* sym = sym_table;
            while (sym != NULL) {
                if (strcmp(sym->name, node->value) == 0) break;
                sym = sym->next;
            }
            if (sym == NULL) {
                /* Should not happen after semantic checks, but be safe */
                fprintf(stderr, "codegen: unbound variable '%s'\n", node->value);
                exit(1);
            }
            /* Emit the substituted regex tree using a fresh id */
            int sub_id = next_id();
            emit_node(sym->regex_tree, out, sub_id, sym_table);
            /* Emit a thin wrapper so our caller can call match_<id> */
            fprintf(out,
                "/* SUB ${%s} -> delegates to match_%d */\n"
                "static int match_%d(const char* s, int pos, int len) {\n"
                "    return match_%d(s, pos, len);\n"
                "}\n\n", node->value, sub_id, id, sub_id);
            return id;
        }

        /* ---- NODE_DEF: skip (handled via symbol table in NODE_SUB) ---- */
        case NODE_DEF:
            return -1;

        default:
            fprintf(stderr, "codegen: unhandled node type %d\n", node->type);
            exit(1);
    }
}

/* -----------------------------------------------------------------------
 * count_nodes
 *
 * Dry-run pass: mirrors the id allocation logic of emit_node exactly,
 * but writes nothing.  Returns the total number of match_N functions
 * that emit_node will produce so we can forward-declare all of them
 * before emitting any bodies.
 * ----------------------------------------------------------------------- */
static int count_nodes(Node* node, Symbol* sym_table) {
    if (node == NULL) return 0;

    switch (node->type) {
        case NODE_PROGRAM:
            /* allocates 1 id (root_id), then recurses into right */
            return 1 + count_nodes(node->right, sym_table);

        case NODE_LITERAL:
        case NODE_WILD:
        case NODE_ID:
            /* leaf: allocates exactly 1 id (its own) */
            return 1;

        case NODE_SEQ:
        case NODE_ALT:
            if (node->right == NULL) {
                /* negation / single-child alt */
                return 1 + count_nodes(node->left, sym_table);
            }
            return 1 + count_nodes(node->left, sym_table)
                     + count_nodes(node->right, sym_table);

        case NODE_REPEAT:
            /* allocates 1 for child + 1 for itself */
            return 1 + count_nodes(node->left, sym_table);

        case NODE_RANGE:
            if (node->left != NULL && node->right != NULL &&
                node->left->type == NODE_RANGE && node->right->type != NODE_RANGE) {
                /* X-Y leaf range: just 1 */
                return 1;
            } else if (node->left != NULL && node->right != NULL) {
                /* union: 1 + children */
                return 1 + count_nodes(node->left, sym_table)
                         + count_nodes(node->right, sym_table);
            }
            return 1;

        case NODE_SUB: {
            /* 1 for the wrapper + however many the substituted tree needs */
            Symbol* sym = sym_table;
            while (sym != NULL) {
                if (strcmp(sym->name, node->value) == 0) break;
                sym = sym->next;
            }
            if (sym == NULL) return 1;
            return 1 + count_nodes(sym->regex_tree, sym_table);
        }

        case NODE_DEF:
            return 0;

        default:
            return 0;
    }
}

/* -----------------------------------------------------------------------
 * codegen
 *
 * Top-level entry point.  Emits a complete rexec.c:
 *   1. Standard headers
 *   2. Forward declarations for every match_N function
 *   3. All the match_N() function bodies (via emit_node)
 *   4. A main() that reads a file and calls the root matcher
 * ----------------------------------------------------------------------- */
void codegen(Node* root, FILE* out) {
    if (root == NULL || root->type != NODE_PROGRAM) {
        fprintf(stderr, "codegen: expected NODE_PROGRAM at root\n");
        exit(1);
    }

    extern Symbol* symbol_top;

    /* ---- header ---- */
    fprintf(out,
        "/* rexec.c -- generated by regex compiler */\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n\n"
    );

    /* ---- pass 1: count how many functions will be emitted ---- */
    g_counter = 0;
    int total = count_nodes(root, symbol_top);

    /* ---- emit forward declarations for all of them ---- */
    fprintf(out, "/* forward declarations */\n");
    for (int i = 0; i < total; i++) {
        fprintf(out, "static int match_%d(const char* s, int pos, int len);\n", i);
    }
    fprintf(out, "\n");

    /* ---- pass 2: emit function bodies ---- */
    g_counter = 0;
    int root_match_id = next_id();
    emit_node(root->right, out, root_match_id, symbol_top);

    /* ---- main() ---- */
    fprintf(out,
        "/* ---- runtime ---- */\n\n"
        "static char* read_file(const char* path, int* out_len) {\n"
        "    FILE* f = fopen(path, \"rb\");\n"
        "    if (!f) { perror(path); exit(1); }\n"
        "    fseek(f, 0, SEEK_END);\n"
        "    long sz = ftell(f);\n"
        "    rewind(f);\n"
        "    char* buf = (char*)malloc(sz + 1);\n"
        "    if (!buf) { fprintf(stderr, \"out of memory\\n\"); exit(1); }\n"
        "    fread(buf, 1, sz, f);\n"
        "    buf[sz] = '\\0';\n"
        "    fclose(f);\n"
        "    /* strip trailing newline that editors append */\n"
        "    while (sz > 0 && (buf[sz-1] == '\\n' || buf[sz-1] == '\\r')) {\n"
        "        buf[--sz] = '\\0';\n"
        "    }\n"
        "    *out_len = (int)sz;\n"
        "    return buf;\n"
        "}\n\n"
        "int main(int argc, char** argv) {\n"
        "    if (argc < 2) {\n"
        "        fprintf(stderr, \"Usage: %%s <file>\\n\", argv[0]);\n"
        "        return 1;\n"
        "    }\n"
        "    int len = 0;\n"
        "    char* input = read_file(argv[1], &len);\n"
        "    int end = match_%d(input, 0, len);\n"
        "    if (end == len) {\n"
        "        printf(\"ACCEPTS\\n\");\n"
        "    } else {\n"
        "        printf(\"REJECTS\\n\");\n"
        "    }\n"
        "    free(input);\n"
        "    return 0;\n"
        "}\n", root_match_id);
}
