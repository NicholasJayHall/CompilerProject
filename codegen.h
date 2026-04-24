#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdio.h>
#include "tree.h"
#include "semantics.h"

/*
 * Main entry point. Call this after parsing and semantic checks pass.
 * Writes a complete, self-contained rexec.c into `out`.
 *
 * The generated file, when compiled and run as:
 *     ./rexec somefile.txt
 * will print ACCEPTS or REJECTS based on whether the file contents
 * match the regex defined in the original input.
 */
void codegen(Node* root, FILE* out);

/*
 * Internal helpers -- you don't need to call these directly,
 * but they are exposed here for clarity / testing.
 *
 * Each emit_* function writes a single C function into `out`
 * that matches the corresponding AST node. Every generated
 * function has this signature:
 *
 *     int match_N(const char* s, int pos, int len);
 *
 * Returns the new position after a successful match, or -1 on failure.
 * `len` is the total length of the input string (used for bounds checks).
 *
 * `id` is a unique integer used to name the generated function so that
 * multiple nodes of the same type don't collide.
 */
int emit_node(Node* node, FILE* out, int id, Symbol* sym_table);

#endif
