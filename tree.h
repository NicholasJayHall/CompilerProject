#ifndef TREE_H
#define TREE_H

/* NodeType distinguishes what kind of regex operation the node represents */
typedef enum { 
    NODE_PROGRAM,  /* The true root linking definitions and the main regex */
    NODE_DEF,      /* A constant definition */
    NODE_ALT,      /* Alternation (|) */
    NODE_SEQ,      /* Sequence (Implicit) */
    NODE_REPEAT,   /* Repeats (*, +, ?) */
    NODE_LITERAL,  /* Quoted strings */
    NODE_ID,       /* Identifiers/Constants */
    NODE_WILD,     /* The dot (.) */
    NODE_SUB,      /* Substitutions ${ID} */
    NODE_RANGE     /* Character classes [ ] */
} NodeType;

/* The core data structure for the Parse Tree */
typedef struct Node {
    NodeType type;          /* The category of this node */
    char *value;            /* Stores the text for IDs or Literals */
    char op;                /* Stores the specific operator: '*', '+', or '?' */
    struct Node *left;      /* Pointer to the first child/sub-expression */
    struct Node *right;     /* Pointer to the second child (for Alt/Seq) */
} Node;

void print_tree(Node* node, int depth);

/* Function prototypes provided in tree.c */
Node* create_node(NodeType type, Node* left, Node* right);
Node* create_leaf(NodeType type, char* value);
void print_tree(Node* node, int depth);
void free_tree(Node* node);

#endif
