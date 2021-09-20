#ifndef PRSCFL_H
#define PRSCFL_H

#include <stdbool.h>
#include <sys/types.h>

typedef struct prscfl_yy_extra_type {
	/*
	 * string
	 */
	char *strbuf;
	int length;
	int total;

	int lineno;

} prscfl_yy_extra_type;

/*
 * The type of yyscanner is opaque outside scan.l.
 */

typedef void *prscfl_yyscan_t;
int prscflGetLineNo(prscfl_yyscan_t scanner);
prscfl_yyscan_t prscflScannerInit(FILE *fh, prscfl_yy_extra_type *yyext);
void prscflScannerFinish(prscfl_yyscan_t scanner);

typedef struct ValueDef {
	enum {
		undefType	= 0,

		int32Type	= 1,
		int64Type	= 2,
		uint32Type	= 3,
		uint64Type	= 4,
		doubleType	= 5,
		stringType	= 6,
		boolType	= 7,
		commentType = 8,
		structType	= 9,
		arrayType	= 10,
		builtinType = 11
	} type;

	int flags;

	union {
		int32_t			int32val;
		int64_t			int64val;
		u_int32_t		uint32val;
		u_int64_t		uint64val;
		double			doubleval;
		bool			boolval;
		char			*stringval;
		char			*commentval;
		struct ParamDef *structval;
		struct ParamDef *arrayval;
		char			*builtinval;
	} value;
} ValueDef;

typedef struct ParamDef ParamDef;

struct ParamDef {
	char			*name;

	ParamDef		*def;
	ValueDef		value;

	int				flags;

	struct ParamDef	*comment;
	struct ParamDef	*parent;
	struct ParamDef	*next;
};

#define PARAMDEF_RDONLY			(0x01)
#define PARAMDEF_REQUIRED		(0x02)

ParamDef* parseCfgDef(FILE *fh);
void hDump(FILE *fh, ParamDef *def);
void dumpStructName(FILE *fh, ParamDef *def, char *delim);
void dumpParamType(FILE *fh, ParamDef *def);
void cDump(FILE *fh, ParamDef *def);
void fDump(FILE *fh, ParamDef *def);
void pDump(FILE *fh, ParamDef *def);
void HDump(FILE *fh);
void dDump(ParamDef *def);

#endif
