#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tree.h"
#include "semantics.h"

Symbol* symbol_top = NULL;


void add_symbol(char* name, Node* regex_tree){
   // 1. Check if the symbol is already defined to prevent duplicates
    if (symbol_exists(name)) {
        fprintf(stderr, "Semantic Error: Symbol '%s' is already defined.\n", name);
        exit(1);
    }

    // 2. Allocate memory for the new symbol
    Symbol* new_sym = (Symbol*)malloc(sizeof(Symbol));
    if (!new_sym) {
        fprintf(stderr, "Out of memory allocating symbol table entry\n");
        exit(1);
    }

    // 3. Initialize the symbol and add it to the front of the list
    new_sym->name = strdup(name); // Duplicate the string just to be safe
    new_sym->regex_tree = regex_tree;
    new_sym->next = symbol_top;
    symbol_top = new_sym;   


}

int symbol_exists(char* name){
   Symbol* current = symbol_top;
    while (current != NULL) {
        if (strcmp(current->name, name) == 0) {
            return 1; // Found it
        }
        current = current->next;
    }
    return 0; // Not found


}

void check_semantics(Node* node) {
    if (node == NULL) return;

    switch (node->type) {
        case NODE_DEF:
            // node->left contains the ID, node->right contains the Regex
            add_symbol(node->left->value, node->right);
            break;

        case NODE_SUB:
            if (!symbol_exists(node->value)) {
                fprintf(stderr, "Semantic Error: Unbound variable '${%s}'\n", node->value);
                exit(1);
            }
            break;

        case NODE_LITERAL:
        case NODE_RANGE:
            // Ensure the node actually has a text value
            if (node->value != NULL) {
                char* ptr = node->value;
                
                // Loop to find all occurrences of "%x" in the string
                while ((ptr = strstr(ptr, "%x")) != NULL) {
                    ptr += 2; // Move the pointer past the "%x"
                    
                    int codepoint = 0;
                    // Extract the decimal number into our codepoint variable
                    if (sscanf(ptr, "%d", &codepoint) == 1) {
                        // Validate against the Unicode maximum
                        if (codepoint > 1114111) {
                            fprintf(stderr, "Semantic Error: Unicode code point %%x%d; is out of bounds.\n", codepoint);
                            exit(1);
                        }
                    }
                    
                    // Move the pointer past the closing ';' so we can search for the next one
                    ptr = strchr(ptr, ';');
                    if (ptr) {
                        ptr++; 
                    } else {
                        break; 
                    }
                }
            }
            break;

        default:
            break;
    }

    // Continue walking the tree. This will naturally traverse into 
    // the definitions to check their Unicode strings!
    check_semantics(node->left);
    check_semantics(node->right);
}

void free_symbol_table() {
    Symbol* current = symbol_top;
    while (current != NULL) {
        Symbol* next = current->next;
        free(current->name);
        // Note: The regex_tree nodes will be freed by your existing free_tree() logic in main
        free(current);
        current = next;
    }
    symbol_top = NULL;
}
