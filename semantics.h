#include "tree.h"

typedef struct Symbol {
  char* name;
  Node* regex_tree;
  struct Symbol* next;

} Symbol;

void add_symbol(char* name, Node* regex_tree);
int symbol_exists(char* name);
void check_semantics(Node* node);

void free_symbol_table();
