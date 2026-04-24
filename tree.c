#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tree.h"

static Node* allocate_node() {
    Node* node = (Node*)malloc(sizeof(Node));
    if (!node) {
        fprintf(stderr, "Out of memory while building parse tree\n");
        exit(1);
    }
    node->type = NODE_WILD;
    node->value = NULL;
    node->op = '\0';
    node->left = NULL;
    node->right = NULL;
    return node;
}

Node* create_node(NodeType type, Node* left, Node* right) {
    Node* node = allocate_node();
    node->type = type;
    node->left = left;
    node->right = right;
    return node;
}

Node* create_leaf(NodeType type, char* value) {
    Node* node = allocate_node();
    node->type = type;
    if (value) {
        node->value = value;
    }
    return node;
}

void print_tree(Node* node, int depth) {
    if (!node) return;

    for (int i = 0; i < depth; i++) printf("  ");

    switch (node->type) {
        case NODE_ALT:     printf("ALT (|)\n"); break;
        case NODE_SEQ:     printf("SEQ\n"); break;
        case NODE_REPEAT:  printf("REPEAT (%c)\n", node->op); break;
        case NODE_LITERAL: printf("LITERAL: %s\n", node->value); break;
        case NODE_ID:      printf("ID: %s\n", node->value); break;
        case NODE_WILD:    printf("WILD (.)\n"); break;
        case NODE_SUB:     printf("SUBSTITUTE: ${%s}\n", node->value); break;
	case NODE_RANGE:   printf("RANGE\n"); break;
        default:           printf("UNKNOWN NODE\n"); break;
    }

    print_tree(node->left, depth + 1);
    print_tree(node->right, depth + 1);
}

void free_tree(Node* node) {
    if (!node) return;
    free_tree(node->left);
    free_tree(node->right);
    if (node->value) free(node->value);
    free(node);
}

