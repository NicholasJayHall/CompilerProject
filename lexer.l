%{
#include <stdio.h>
#include <string.h>
#include "parser.tab.h"

%}

%option noyywrap

%%

[ \t\r\n]+         
"//".* 		   

"const"             { return CONST; }
"="                 { return EQUALS; }
"/"                 { return SLASH; }
"|"                 { return PIPE; }
"-"		    { return DASH; }
"_"		    { return UNDERSCORE; }
"&"                 { return AMPERSAND; }
"!"                 { return EXCLAM; }
"*"                 { return STAR; }
"+"                 { return PLUS; }
"?"                 { return QUESTION; }
"("                 { return LPAREN; }
")"                 { return RPAREN; }
"["                 { return LBRACKET; }
"]"                 { return RBRACKET; }
"^"                 { return CARET; }
"."                 { return DOT; }
"\${"               { return SUB_START; }
"}"                 { return SUB_END; }

\"([^\"%]|%x[0-9]+;)*\" { 
    yylval.sval = strdup(yytext); 
    return LITERAL; 
}

[a-zA-Z0-9_]+ { 
    yylval.sval = strdup(yytext); 
    return ID; 
}

.                   { printf("Unknown character: %s\n", yytext); return ERROR; }

%%
