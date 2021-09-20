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
static ParamDef *makeParam(char *name, ValueDef *value);
static bool prscflParamCheck(ParamDef *p);
static void propagateFlags(ParamDef *def, int flags);
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
%type	<value>		def scalar_def
%type	<value>		scalar_value value opt_value
%type	<node>		list_value value_list
%type	<node>		param_def_list commented_param_def param_def
%type	<node>		param_list commented_param param
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
	BUILTIN_P param_def_list			{
											ValueDef	v;
											ParamDef	*b;

											MakeValue(v, builtin, $1);
											b = makeParam(NULL, &v);
											MakeList($$, b, $2);
											output = $$;
										}
	| param_def_list					{ output = $$ = $1; }
	;

identifier:
	KEY_P								{ $$ = $1; }
	;

comment:
	COMMENT_P							{
											ValueDef    v;

											MakeValue(v, comment, $1);
											$$ = makeParam(NULL, &v);
										}
	| COMMENT_P comment					{
											ValueDef	v;
											ParamDef	*comment;

											MakeValue(v, comment, $1);
											comment = makeParam(NULL, &v);
											MakeList($$, comment, $2);
										}
	;

comment_opt:
	comment								{ $$ = $1; }
	| /* EMPTY */						{ $$ = NULL; }
	;

flag:
	RDWR_P								{ $$ = 0; free($1); }
	| RDONLY_P							{ $$ = PARAMDEF_RDONLY; free($1); }
	| REQUIRED_P						{ $$ = PARAMDEF_REQUIRED; free($1); }
	;

flag_list:
	flag								{ $$ = $1; }
	| flag_list ',' flag				{ $$ |= $3; }
	;

flags_opt:
	flag_list							{ $$ = $1; }
	| /* EMPTY */						{ $$ = 0; }
	;
	
param_def_list:
	commented_param_def						{ $$ = $1; }
	| commented_param_def param_def_list	{ MakeList($$, $1, $2); }
	;

commented_param_def:
	comment_opt param_def				{
											$$ = $2;

											$$->comment = $1;
										}
	;

param_def:
	param									{
												$$ = $1;
												$$->def = $$;

												if ($$->value.type == structType || $$->value.type == arrayType) {
													prscfl_yyerror(yyscanner, "compound param must have definition");
													YYERROR;
												}
											}
	| def param								{
												$$ = $2;
												$$->def = makeParam($$->name, &$1);
												$$->def->flags = $$->flags;

												if (!prscflParamCheck($$)) {
													prscfl_yyerror(yyscanner, "param value must match definition");
													YYERROR;
												}
												if ($$->def->value.type == structType)
													propagateFlags($$->def->value.value.structval,
																   ($$->def->flags & PARAMDEF_RDONLY));
											}
	;

param:
	flags_opt identifier opt_value			{
												$$ = makeParam($2, &$3);
												$$->flags = $1;
											}
	;

opt_value:
	'=' value								{ $$ = $2; }
	| /* EMPTY */							{ $$ = (ValueDef) { .type = undefType }; }
	;

def:
	flags_opt '{' param_def_list '}'	{
											MakeValue($$, struct, $3);
											$$.flags = $1;
										}
	| flags_opt '[' scalar_def ']'		{
											$$ = $3;
											$$.flags = $1;
										}
	| flags_opt '[' param_def_list ']'	{
											ValueDef v;
											ParamDef *p;

											MakeValue(v, struct, $3);
											p = makeParam(NULL, &v);

											MakeValue($$, array, p);
											$$.flags = $1;
										}
	| flags_opt '[' comment_opt def opt_value ']'	{
											ParamDef *p;

											p = makeParam(NULL, &$5);
											p->comment = $3;
											p->def = makeParam(NULL, &$4);
                                        
											MakeValue($$, array, p);
											$$.flags = $1;
										}
	;

scalar_def:
	comment_opt flags_opt scalar_value	{
											ParamDef *p = makeParam(NULL, &$3);

											p->comment = $1;
											p->def = p;

											MakeValue($$, array, p);
											$$.flags = $2;
										}
	;

scalar_value:
	INT32_P								{ MakeValue($$, int32, $1); }
	| UINT32_P							{ MakeValue($$, uint32, $1); }
	| INT64_P							{ MakeValue($$, int64, $1); }
	| UINT64_P							{ MakeValue($$, uint64, $1); }
	| DOUBLE_P							{ MakeValue($$, double, $1); }
	| STRING_P							{ MakeValue($$, string, $1); }
	| NULL_P							{ MakeValue($$, string, NULL); }
	| TRUE_P							{ MakeValue($$, bool, true); free($1); }
	| FALSE_P							{ MakeValue($$, bool, false); free($1); }
	;

value:
	scalar_value						{ $$ = $1; }
	| '{' param_list '}'				{ MakeValue($$, struct, $2); }
	| '[' value_list ']'				{ MakeValue($$, array, $2);	}
	;

param_list:
	commented_param						{ $$ = $1; }
	| commented_param param_list		{ MakeList($$, $1, $2); }
	;

commented_param:
	comment_opt param					{
											$$ = $2;
											$$->comment = $1;
										}
	;

value_list:
	list_value							{ $$ = $1; }
	| list_value value_list				{ MakeList($$, $1, $2); }
	;

list_value:
	comment_opt value					{
											$$ = makeParam(NULL, &$2);
											$$->flags = $$->value.flags;
											$$->comment = $1;
										}
	;

%%

static int
prscfl_yyerror(prscfl_yyscan_t yyscanner, const char *msg) {
    fprintf(stderr, "gram_yyerror: %s at line %d\n", msg, prscflGetLineNo(yyscanner));
	return 0;
}

static ParamDef *makeParam(char *name, ValueDef *value)
{
	ParamDef *p = malloc(sizeof(*p));

	p = malloc(sizeof(*p));

	*p = (ParamDef) {
		.name = name,

		.value = *value
	};

	return p;
}

static void
prscflParamCommentMerge(ParamDef *from, ParamDef *to)
{
	ParamDef *comment = from->comment;

	if (!comment)
		return;

	while (comment->next)
		comment = comment->next;

	comment->next = to->comment;
	to->comment = from->comment;
	from->comment = NULL;
}

static bool
prscflParamCheck(ParamDef *p)
{
	ParamDef *def = p->def;

	if (p->value.type == undefType)
		p->value.type = def->value.type;
	else if (def->value.type != p->value.type)
		return false;

	switch (def->value.type) {
		case structType: {
			ParamDef *dp, *vp_new = NULL, *vp_new_end = NULL;

			for (dp = def->value.value.structval; dp; dp = dp->next) {
				ParamDef *vp, *prev_vp, *vp_last = NULL;

				for (prev_vp = NULL, vp = p->value.value.structval; vp; prev_vp = vp, vp = vp->next) {
					if (strcmp(dp->name, vp->name) != 0)
						continue;

					if (!prev_vp)
						p->value.value.structval = vp->next;
					else
						prev_vp->next = vp->next;

					prscflParamCommentMerge(dp, vp);

					vp_last = vp;
				}

				if (!vp_last)
					vp_last = makeParam(dp->name, &dp->value);
				else
					vp_last->next = NULL;

				vp_last->def = dp->def;

				if (!prscflParamCheck(vp_last))
					return false;
				
				if (!vp_new)
					vp_new = vp_last;
				else
					vp_new_end->next = vp_last;
				vp_new_end = vp_last;
			}

			if (p->value.value.structval != NULL)
				return false;

			p->value.value.structval = vp_new;

			SetParent(def, def->value.value.structval);
			SetParent(p, p->value.value.structval);

			break;
		}
		case arrayType: {
			ParamDef *vp;

			for (vp = p->value.value.arrayval; vp != NULL; vp = vp->next) {
				vp->def = def->value.value.arrayval->def;

				if (!prscflParamCheck(vp))
					return false;
			}

			SetParent(def, def->value.value.arrayval);
			SetParent(p, p->value.value.arrayval);

			if (p->value.value.arrayval)
				prscflParamCommentMerge(def->value.value.arrayval, p->value.value.arrayval);
		}
		default:
			break;
	}

	return true;
}

static void propagateFlags(ParamDef *def, int flags)
{
	if (!flags)
		return;

	def->flags |= flags;

	if (def->value.type == structType)
		def = def->value.value.structval;
	else if (def->value.type == arrayType)
		def = def->value.value.arrayval;
	else
		return;

	while (def) {
		propagateFlags(def, flags);
		def = def->next;
	}
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

