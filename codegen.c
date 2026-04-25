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
    if (!raw) return;
    const char* p = raw + 1;
    fputc('"', out);
    while (*p && !(*p == '"' && *(p+1) == '\0') && *p != '\0') {
        if (*p == '"') break;
        if (*p == '%' && *(p+1) == 'x') {
            p += 2;
            int cp = 0;
            while (*p >= '0' && *p <= '9') {
                cp = cp * 10 + (*p - '0');
                p++;
            }
            if (*p == ';') p++;
            char utf8[4];
            int nbytes = codepoint_to_utf8(cp, utf8);
            for (int i = 0; i < nbytes; i++) {
                fprintf(out, "\\x%02x", (unsigned char)utf8[i]);
            }
        } else if (*p == '\\') {
            fputc(*p, out); p++;
            if (*p) { fputc(*p, out); p++; }
        } else {
            if (*p == '"') fputs("\\\"", out);
            else if (*p == '\\') fputs("\\\\", out);
            else fputc(*p, out);
            p++;
        }
    }
    fputc('"', out);
}

static int literal_byte_length(const char* raw) {
    if (!raw) return 0;
    const char* p = raw + 1;
    int len = 0;
    while (*p && *p != '"') {
        if (*p == '%' && *(p+1) == 'x') {
            p += 2;
            int cp = 0;
            while (*p >= '0' && *p <= '9') { cp = cp * 10 + (*p - '0'); p++; }
            if (*p == ';') p++;
            if      (cp <= 0x7F)   len += 1;
            else if (cp <= 0x7FF)  len += 2;
            else if (cp <= 0xFFFF) len += 3;
            else                   len += 4;
        } else {
            len++; p++;
        }
    }
    return len;
}

static void emit_range_char(const char* raw, FILE* out) {
    if (!raw || !raw[0]) return;
    if (raw[0] == '%' && raw[1] == 'x') {
        const char* p = raw + 2;
        int cp = 0;
        while (*p >= '0' && *p <= '9') { cp = cp * 10 + (*p - '0'); p++; }
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

int emit_node(Node* node, FILE* out, int id, Symbol* sym_table) {
    if (node == NULL) return -1;

    switch (node->type) {
        case NODE_PROGRAM: {
            int root_id = next_id();
            emit_node(node->right, out, root_id, sym_table);
            return root_id;
        }

        case NODE_LITERAL: {
            int byte_len = literal_byte_length(node->value);
            fprintf(out,
                "/* LITERAL */\n"
                "static void match_%d(const char* s, int pos, int len, char* out_set) {\n"
                "    const char* lit = ", id);
            emit_literal_string(node->value, out);
            fprintf(out, ";\n"
                "    int litlen = %d;\n"
                "    if (pos + litlen <= len && memcmp(s + pos, lit, litlen) == 0) {\n"
                "        out_set[pos + litlen] = 1;\n"
                "    }\n"
                "}\n\n", byte_len);
            return id;
        }

        case NODE_WILD: {
            fprintf(out,
                "/* WILD */\n"
                "static void match_%d(const char* s, int pos, int len, char* out_set) {\n"
                "    if (pos < len) out_set[pos + 1] = 1;\n"
                "}\n\n", id);
            return id;
        }

        case NODE_SEQ: {
            int left_id  = next_id();
            int right_id = next_id();
            emit_node(node->left,  out, left_id,  sym_table);
            emit_node(node->right, out, right_id, sym_table);
            fprintf(out,
                "/* SEQ */\n"
                "static void match_%d(const char* s, int pos, int len, char* out_set) {\n"
                "    char* left_set = (char*)calloc(len + 1, 1);\n"
                "    match_%d(s, pos, len, left_set);\n"
                "    for (int i = pos; i <= len; i++) {\n"
                "        if (left_set[i]) {\n"
                "            match_%d(s, i, len, out_set);\n"
                "        }\n"
                "    }\n"
                "    free(left_set);\n"
                "}\n\n", id, left_id, right_id);
            return id;
        }

        case NODE_ALT: {
            if (node->right == NULL) {
                int child_id = next_id();
                emit_node(node->left, out, child_id, sym_table);
                fprintf(out,
                    "/* NEGATION */\n"
                    "static void match_%d(const char* s, int pos, int len, char* out_set) {\n"
                    "    char* child_set = (char*)calloc(len + 1, 1);\n"
                    "    match_%d(s, pos, len, child_set);\n"
                    "    int matched = 0;\n"
                    "    for (int i = pos; i <= len; i++) {\n"
                    "        if (child_set[i]) { matched = 1; break; }\n"
                    "    }\n"
                    "    free(child_set);\n"
                    "    if (!matched && pos < len) out_set[pos + 1] = 1;\n"
                    "}\n\n", id, child_id);
            } else {
                int left_id  = next_id();
                int right_id = next_id();
                emit_node(node->left,  out, left_id,  sym_table);
                emit_node(node->right, out, right_id, sym_table);
                fprintf(out,
                    "/* ALT */\n"
                    "static void match_%d(const char* s, int pos, int len, char* out_set) {\n"
                    "    match_%d(s, pos, len, out_set);\n"
                    "    match_%d(s, pos, len, out_set);\n"
                    "}\n\n", id, left_id, right_id);
            }
            return id;
        }

        case NODE_REPEAT: {
            int child_id = next_id();
            emit_node(node->left, out, child_id, sym_table);

            if (node->op == '*') {
                fprintf(out,
                    "/* REPEAT * */\n"
                    "static void match_%d(const char* s, int pos, int len, char* out_set) {\n"
                    "    char* cur_set = (char*)calloc(len + 1, 1);\n"
                    "    char* next_set = (char*)calloc(len + 1, 1);\n"
                    "    cur_set[pos] = 1;\n"
                    "    out_set[pos] = 1;\n"
                    "    int changed = 1;\n"
                    "    while (changed) {\n"
                    "        changed = 0;\n"
                    "        memset(next_set, 0, len + 1);\n"
                    "        for (int i = pos; i <= len; i++) {\n"
                    "            if (cur_set[i]) match_%d(s, i, len, next_set);\n"
                    "        }\n"
                    "        for (int i = pos; i <= len; i++) {\n"
                    "            if (next_set[i] && !out_set[i]) {\n"
                    "                out_set[i] = 1;\n"
                    "                cur_set[i] = 1;\n"
                    "                changed = 1;\n"
                    "            } else {\n"
                    "                cur_set[i] = 0;\n"
                    "            }\n"
                    "        }\n"
                    "    }\n"
                    "    free(cur_set);\n"
                    "    free(next_set);\n"
                    "}\n\n", id, child_id);
            } else if (node->op == '+') {
                fprintf(out,
                    "/* REPEAT + */\n"
                    "static void match_%d(const char* s, int pos, int len, char* out_set) {\n"
                    "    char* cur_set = (char*)calloc(len + 1, 1);\n"
                    "    char* next_set = (char*)calloc(len + 1, 1);\n"
                    "    cur_set[pos] = 1;\n"
                    "    int changed = 1;\n"
                    "    while (changed) {\n"
                    "        changed = 0;\n"
                    "        memset(next_set, 0, len + 1);\n"
                    "        for (int i = pos; i <= len; i++) {\n"
                    "            if (cur_set[i]) match_%d(s, i, len, next_set);\n"
                    "        }\n"
                    "        for (int i = pos; i <= len; i++) {\n"
                    "            if (next_set[i] && !out_set[i]) {\n"
                    "                out_set[i] = 1;\n"
                    "                cur_set[i] = 1;\n"
                    "                changed = 1;\n"
                    "            } else {\n"
                    "                cur_set[i] = 0;\n"
                    "            }\n"
                    "        }\n"
                    "    }\n"
                    "    free(cur_set);\n"
                    "    free(next_set);\n"
                    "}\n\n", id, child_id);
            } else { 
                fprintf(out,
                    "/* REPEAT ? */\n"
                    "static void match_%d(const char* s, int pos, int len, char* out_set) {\n"
                    "    out_set[pos] = 1;\n"
                    "    match_%d(s, pos, len, out_set);\n"
                    "}\n\n", id, child_id);
            }
            return id;
        }

        case NODE_RANGE: {
            if (node->left == NULL && node->right != NULL && node->right->type == NODE_RANGE) {
                fprintf(out,
                    "/* RANGE item X-Y */\n"
                    "static void match_%d(const char* s, int pos, int len, char* out_set) {\n"
                    "    if (pos < len) {\n"
                    "        unsigned char c = (unsigned char)s[pos];\n"
                    "        if (c >= ", id);
                emit_range_char(node->value, out);
                fprintf(out, " && c <= ");
                emit_range_char(node->right->value, out);
                fprintf(out, ") out_set[pos + 1] = 1;\n"
                    "    }\n"
                    "}\n\n");
            } else if (node->left != NULL && node->right != NULL) {
                int left_id  = next_id();
                int right_id = next_id();
                emit_node(node->left,  out, left_id,  sym_table);
                emit_node(node->right, out, right_id, sym_table);
                fprintf(out,
                    "/* RANGE union */\n"
                    "static void match_%d(const char* s, int pos, int len, char* out_set) {\n"
                    "    match_%d(s, pos, len, out_set);\n"
                    "    match_%d(s, pos, len, out_set);\n"
                    "}\n\n", id, left_id, right_id);
            } else {
                fprintf(out,
                    "/* RANGE single char */\n"
                    "static void match_%d(const char* s, int pos, int len, char* out_set) {\n"
                    "    if (pos < len) {\n"
                    "        unsigned char c = (unsigned char)s[pos];\n"
                    "        if (c == ", id);
                emit_range_char(node->value, out);
                fprintf(out, ") out_set[pos + 1] = 1;\n"
                    "    }\n"
                    "}\n\n");
            }
            return id;
        }

        case NODE_ID: {
            fprintf(out,
                "/* RANGE ID char */\n"
                "static void match_%d(const char* s, int pos, int len, char* out_set) {\n"
                "    if (pos < len) {\n"
                "        const char* chars = \"%s\";\n"
                "        int clen = (int)strlen(chars);\n"
                "        for (int i = 0; i < clen; i++) {\n"
                "            if (s[pos] == chars[i]) {\n"
                "                out_set[pos + 1] = 1;\n"
                "                break;\n"
                "            }\n"
                "        }\n"
                "    }\n"
                "}\n\n", id, node->value ? node->value : "");
            return id;
        }

        case NODE_SUB: {
            Symbol* sym = sym_table;
            while (sym != NULL) {
                if (strcmp(sym->name, node->value) == 0) break;
                sym = sym->next;
            }
            if (sym == NULL) {
                fprintf(stderr, "codegen: unbound variable '%s'\n", node->value);
                exit(1);
            }
            int sub_id = next_id();
            emit_node(sym->regex_tree, out, sub_id, sym_table);
            fprintf(out,
                "/* SUB ${%s} -> delegates to match_%d */\n"
                "static void match_%d(const char* s, int pos, int len, char* out_set) {\n"
                "    match_%d(s, pos, len, out_set);\n"
                "}\n\n", node->value, sub_id, id, sub_id);
            return id;
        }

        case NODE_DEF:
            return -1;

        default:
            fprintf(stderr, "codegen: unhandled node type %d\n", node->type);
            exit(1);
    }
}

static int count_nodes(Node* node, Symbol* sym_table) {
    if (node == NULL) return 0;

    switch (node->type) {
        case NODE_PROGRAM:
            return 1 + count_nodes(node->right, sym_table);

        case NODE_LITERAL:
        case NODE_WILD:
        case NODE_ID:
            return 1;

        case NODE_SEQ:
        case NODE_ALT:
            if (node->right == NULL) {
                return 1 + count_nodes(node->left, sym_table);
            }
            return 1 + count_nodes(node->left, sym_table)
                     + count_nodes(node->right, sym_table);

        case NODE_REPEAT:
            return 1 + count_nodes(node->left, sym_table);

        case NODE_RANGE:
            if (node->left == NULL && node->right != NULL && node->right->type == NODE_RANGE) {
                return 1;
            } else if (node->left != NULL && node->right != NULL) {
                return 1 + count_nodes(node->left, sym_table)
                         + count_nodes(node->right, sym_table);
            }
            return 1;

        case NODE_SUB: {
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

void codegen(Node* root, FILE* out) {
    if (root == NULL || root->type != NODE_PROGRAM) {
        fprintf(stderr, "codegen: expected NODE_PROGRAM at root\n");
        exit(1);
    }

    extern Symbol* symbol_top;

    fprintf(out,
        "/* rexec.c -- generated by regex compiler */\n"
        "#include <stdio.h>\n"
        "#include <stdlib.h>\n"
        "#include <string.h>\n\n"
    );

    g_counter = 0;
    int total = count_nodes(root, symbol_top);

    fprintf(out, "/* forward declarations */\n");
    for (int i = 0; i < total; i++) {
        fprintf(out, "static void match_%d(const char* s, int pos, int len, char* out_set);\n", i);
    }
    fprintf(out, "\n");

    g_counter = 0;
    int root_match_id = next_id();
    emit_node(root->right, out, root_match_id, symbol_top);

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
        "    char* out_set = (char*)calloc(len + 1, 1);\n"
        "    match_%d(input, 0, len, out_set);\n"
        "    if (out_set[len]) {\n"
        "        printf(\"ACCEPTS\\n\");\n"
        "    } else {\n"
        "        printf(\"REJECTS\\n\");\n"
        "    }\n"
        "    free(out_set);\n"
        "    free(input);\n"
        "    return 0;\n"
        "}\n", root_match_id);
}
