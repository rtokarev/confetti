%{
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <prscfl.h>
#include <prscfl_gram.h>

static int prscfl_yyerror(prscfl_yyscan_t yyscanner, const char *msg);
static ParamDef *makeScalarParam(char *name, ValueDef *value);
static bool prscflParamDefEq(const ParamDef *p1, const ParamDef *p2);
extern int prscfl_yylex (YYSTYPE * yylval_param, prscfl_yyscan_t yyscanner);

static ParamDef	*output;

#define MakeValue(r, t, v) do {	\
	(r) = (ValueDef){			\
		.type	= t##Type,		\
		.value	= {				\
			.t##val	= (v)		\
		}						\
	};							\
} while(0)

#define MakeList(r, f, l)					\
	if (f) {								\
		(f)->next = (l);					\
		(r) = (f);							\
	} else {								\
		(r) = (l);							\
	}

#define SetParent(p, l)	do {				\
	ParamDef *i = (l);						\
	while(i) {								\
		i->parent = (p);					\
		i = i->next;						\
	}										\
} while(0)

#define PropagateStructFlags(s, f) do {								\
	if ((s)->value.type == structType && ((f) & PARAMDEF_RDONLY)) {	\
		ParamDef *child_def = (s)->value.value.structval;			\
																	\
		while(child_def) {											\
			child_def->flags |= PARAMDEF_RDONLY;					\
																	\
			child_def = child_def->next;							\
		}															\
	}																\
} while(0)

%}

%pure-parser
%expect 0
%name-prefix "prscfl_yy"
%error-verbose

%parse-param {prscfl_yyscan_t yyscanner}
%lex-param   {prscfl_yyscan_t yyscanner}

%union		 {
	int32_t		int32val;
	u_int32_t	uint32val;
	int64_t		int64val;
	u_int64_t	uint64val;
	double		doubleval;
	char		*str;

	ValueDef	value;
	ParamDef	*node;
}

%type	<str>		identifier
%type	<value>		value
%type	<node>		list_value value_list
%type	<node>		param param_list
%type	<node>		commented_param
%type	<node>		comment comment_opt
%type	<node>		cfg
%type	<int32val>	flags_opt flag flag_list

%token	<str>		KEY_P NULL_P STRING_P COMMENT_P RDONLY_P RDWR_P
					BUILTIN_P REQUIRED_P FALSE_P TRUE_P
%token	<int32val>	INT32_P
%token	<uint32val>	UINT32_P
%token	<int64val>	INT64_P
%token	<uint64val>	UINT64_P
%token	<doubleval>	DOUBLE_P

%%

cfg:
	BUILTIN_P param_list	{
				ValueDef	v;
				ParamDef	*b;

				MakeValue(v, builtin, $1);
				b = makeScalarParam(NULL, &v);
				MakeList($$, b, $2);
				output = $$;
			}
	| param_list			{ output = $$ = $1; }
	;

identifier:
	KEY_P			{ $$ = $1; }
	| NULL_P		{ $$ = $1; }
	| TRUE_P		{ $$ = $1; }
	| FALSE_P		{ $$ = $1; }
	| RDONLY_P		{ $$ = $1; }
	| RDWR_P		{ $$ = $1; }
	| REQUIRED_P	{ $$ = $1; }
	;

param_list:
	commented_param					{ $$ = $1; }
	| commented_param param_list	{ MakeList($$, $1, $2); }
	;

comment:
	COMMENT_P							{
			ValueDef    v;

			MakeValue(v, comment, $1);
			$$ = makeScalarParam(NULL, &v);
		}
	| COMMENT_P comment					{
			ValueDef	v;
			ParamDef	*comment;

			MakeValue(v, comment, $1);
			comment = makeScalarParam(NULL, &v);
			MakeList($$, comment, $2);
		}
	;

comment_opt:
	comment					{ $$ = $1; }
	| /* EMPTY */			{ $$ = NULL; }
	;

flag:
	RDWR_P					{ $$ = 0; free($1); }
	| RDONLY_P				{ $$ = PARAMDEF_RDONLY; free($1); }
	| REQUIRED_P			{ $$ = PARAMDEF_REQUIRED; free($1); }
	;

flag_list:
	flag					{ $$ = $1; }
	| flag_list ',' flag	{ $$ |= $3; }
	;

flags_opt:
	','	flag_list			{ $$ = $2; }
	| /* EMPTY */			{ $$ = 0; }
	;

commented_param:
	comment_opt param flags_opt 	{
			$$ = $2; $$->comment = $1; $$->flags = $3;
			PropagateStructFlags($2, $3);
		}
	;

value:
	INT32_P												{ MakeValue($$, int32, $1); }
	| UINT32_P											{ MakeValue($$, uint32, $1); }
	| INT64_P											{ MakeValue($$, int64, $1); }
	| UINT64_P											{ MakeValue($$, uint64, $1); }
	| DOUBLE_P											{ MakeValue($$, double, $1); }
	| STRING_P											{ MakeValue($$, string, $1); }
	| NULL_P											{ MakeValue($$, string, NULL); }
	| TRUE_P											{ MakeValue($$, bool, true); free($1); }
	| FALSE_P											{ MakeValue($$, bool, false); free($1); }
	| '{' param_list '}'								{ MakeValue($$, struct, $2); }
	| '[' param_list ']'								{
															ValueDef v;
															ParamDef *p;

															MakeValue(v, struct, $2);
															p = makeScalarParam(NULL, &v);
															SetParent(p, $2);

															MakeValue($$, array, p);
														}
	| '[' value_list ']'								{ MakeValue($$, array, $2);	}
	;

list_value:
	comment_opt value flags_opt	{
									$$ = makeScalarParam(NULL, &$2);
									$$->comment = $1;
									$$->flags = $3;
									if ($2.type == structType) {
										PropagateStructFlags($$, $3);
										SetParent($$, $2.value.structval);
									} else if ($2.type == arrayType)
										SetParent($$, $2.value.arrayval);
								}
	;

value_list:
	list_value				{ $$ = $1; }
	| list_value value_list	{
								if (prscflParamDefEq($1, $2) == false) {
									prscfl_yyerror(yyscanner, "array values must be the same type");
									YYERROR;
								}
								MakeList($$, $1, $2);
							}
	;

param:
	identifier '=' value	{
								$$ = makeScalarParam($1, &$3);
								if ($3.type == structType)
									SetParent($$, $3.value.structval);
								else if ($3.type == arrayType)
									SetParent($$, $3.value.arrayval);
							}
	;

%%

static int
prscfl_yyerror(prscfl_yyscan_t yyscanner, const char *msg) {
    fprintf(stderr, "gram_yyerror: %s at line %d\n", msg, prscflGetLineNo(yyscanner));
	return 0;
}

static ParamDef *makeScalarParam(char *name, ValueDef *value)
{
	ParamDef *p = malloc(sizeof(*p));

	*p = (ParamDef) {
		.value = *value,
		.name = name
	};

	return p;
}

static bool
prscflParamDefEq(const ParamDef *p1, const ParamDef *p2)
{
	if (p1->value.type != p2->value.type)
		return false;

	switch (p1->value.type) {
		case structType:
			p1 = p1->value.value.structval;
			p2 = p2->value.value.structval;

			while (p1 != NULL && p2 != NULL) {
				if (strcmp(p1->name, p2->name) != 0 ||
					prscflParamDefEq(p1, p2) == false)
					return false;

				p1 = p1->next;
				p2 = p2->next;
			}

			if (p1 != NULL || p2 != NULL)
				return false;

			break;
		case arrayType:
			return prscflParamDefEq(p1->value.value.arrayval, p2->value.value.arrayval);
		default:
			break;
	}

	return true;
}

ParamDef*
parseCfgDef(FILE *fh) {
	prscfl_yyscan_t			yyscanner;
	prscfl_yy_extra_type	yyextra;
	int						yyresult;

	yyscanner = prscflScannerInit(fh, &yyextra);

	output = NULL;
	yyresult = prscfl_yyparse(yyscanner);
	prscflScannerFinish(yyscanner);

	if (yyresult != 0) 
		return NULL;

	return output;
}

