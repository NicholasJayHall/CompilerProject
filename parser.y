%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tree.h"
#include "semantics.h"
#include "codegen.h"
#include <libgen.h>

void yyerror(const char *s);
int yylex();

Node *root = NULL;
%}

%union {
    char *sval;
    struct Node *nval;
}

%token <sval> ID LITERAL
%token CONST EQUALS SLASH PIPE AMPERSAND EXCLAM DASH UNDERSCORE
%token STAR PLUS QUESTION LPAREN RPAREN LBRACKET 
%token RBRACKET CARET DOT SUB_START SUB_END ERROR

%left ID
%left DASH
%left AMPERSAND

%type <nval> RootRegex Regex Sequence Repeat Term Literal Range Wild Substitute CharRanges RangeItem Definitions Definition

%%

System:
    Definitions SLASH RootRegex SLASH { 
        root = create_node(NODE_PROGRAM, $1, $3);
    }
    ;

Definitions:
    { $$ = NULL; }
    | Definitions Definition      { 
        // Chain definitions together using your existing NODE_SEQ
        if ($1 == NULL) {
            $$ = $2;
        } else {
            $$ = create_node(NODE_SEQ, $1, $2); 
        }
    }
    ;

Definition:
    CONST ID EQUALS SLASH Regex SLASH {
        // Create a definition node. 
        // Left = The ID name, Right = The Regex tree
        Node* id_node = create_leaf(NODE_ID, $2);
        $$ = create_node(NODE_DEF, id_node, $5);
    }
    ;

RootRegex:
    RootRegex AMPERSAND RootRegex { $$ = create_node(NODE_SEQ, $1, $3); }
    | EXCLAM Regex                { $$ = create_node(NODE_ALT, $2, NULL); }
    | Regex                       { $$ = $1; }
    ;

Regex:
    Regex PIPE Sequence           { $$ = create_node(NODE_ALT, $1, $3); }
    | Sequence                    { $$ = $1; }
    ;

Sequence:
    Sequence Repeat               { $$ = create_node(NODE_SEQ, $1, $2); }
    | Repeat                      { $$ = $1; }
    ;

Repeat:
    Repeat STAR                   { $$ = create_node(NODE_REPEAT, $1, NULL); $$->op = '*'; }
    | Repeat PLUS                   { $$ = create_node(NODE_REPEAT, $1, NULL); $$->op = '+'; }
    | Repeat QUESTION               { $$ = create_node(NODE_REPEAT, $1, NULL); $$->op = '?'; }
    | Term                        { $$ = $1; }
    ;

Term:
    Literal                       { $$ = $1; }
    | Range                       { $$ = $1; }
    | Wild                        { $$ = $1; }
    | Substitute                  { $$ = $1; }
    | LPAREN Regex RPAREN         { $$ = $2; }
    ;

Literal:
    LITERAL                       { $$ = create_leaf(NODE_LITERAL, $1); }
    ;

Range:
    LBRACKET CharRanges RBRACKET        { $$ = $2; }
    | LBRACKET CARET CharRanges RBRACKET { $$ = create_node(NODE_ALT, $3, NULL); } 
    ;

Wild:
    DOT                           { $$ = create_leaf(NODE_WILD, NULL); }
    ;

Substitute:
    SUB_START ID SUB_END          { $$ = create_leaf(NODE_SUB, $2); }
    ;

CharRanges:
    RangeItem                     { $$ = $1; }
    | CharRanges RangeItem        { $$ = create_node(NODE_RANGE, $1, $2); }
    ;

RangeItem:
    ID                            { $$ = create_leaf(NODE_ID, $1); }
    | ID DASH ID %prec DASH       { Node* n = create_leaf(NODE_RANGE, $1); n->right = create_leaf(NODE_RANGE, $3); $$ = n; }
    | PLUS                        { $$ = create_leaf(NODE_ID, strdup("+")); }
    | DASH                        { $$ = create_leaf(NODE_ID, strdup("-")); }
    | UNDERSCORE                  { $$ = create_leaf(NODE_ID, strdup("_")); }
    | EQUALS                      { $$ = create_leaf(NODE_ID, strdup("=")); }
    ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Syntax Error: %s\n", s);
    exit(1);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        return 1;
    }
    FILE *f = fopen(argv[1], "r");
    if (!f) {
        perror("Error opening file");
        return 1;
    }
    extern FILE *yyin;
    yyin = f;

    if (yyparse() == 0 && root != NULL) {
        check_semantics(root);
        char output_path[1024];
	snprintf(output_path, sizeof(output_path), "%s/rexec.c", dirname(argv[1]));
	FILE* out = fopen(output_path, "w");
	codegen(root, out);
	fclose(out);
        free_tree(root);
        free_symbol_table();
    }

    fclose(f);

    return 0;
}
