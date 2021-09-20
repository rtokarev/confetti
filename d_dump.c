#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>

#include <prscfl.h>

static void
putt(int level) {
	while(level--)
		putchar('\t');
}

static void
printDef(char *type, ParamDef *def) {
	if (def->value.type == structType || def->value.type == arrayType) {
		ParamDef *type_def = def->value.type == structType ?
			def->value.value.structval :
			def->value.value.arrayval;

		printf("%s (%s%s)\t%s (%s%s)\n",
					type,
					(type_def->flags & PARAMDEF_RDONLY) ? "RO" : "RW",
					(type_def->flags & PARAMDEF_REQUIRED) ? ", REQ" : "",
					def->name,
					(def->flags & PARAMDEF_RDONLY) ? "RO" : "RW",
					(def->flags & PARAMDEF_REQUIRED) ? ", REQ" : "");
	} else
		printf("%s\t%s (%s%s)\n",
					type,
					def->name,
					(def->flags & PARAMDEF_RDONLY) ? "RO" : "RW",
					(def->flags & PARAMDEF_REQUIRED) ? ", REQ" : "");
}

static void
debugParamDef(ParamDef *def, int level) {

	while(def) {
		putt(level);
		switch(def->value.type) {
			case	int32Type:
				printDef("int32_t", def);
				break;
			case	uint32Type:
				printDef("u_int32_t", def);
				break;
			case	int64Type:
				printDef("int64_t", def);
				break;
			case	uint64Type:
				printDef("u_int64_t", def);
				break;
			case	doubleType:
				printDef("double", def);
				break;
			case	stringType:
				printDef("char*", def);
				break;
			case	boolType:
				printDef("bool", def);
				break;
			case	commentType:
				fprintf(stderr, "Unexpected comment");
				break;
			case	structType:
				printDef("struct", def);
				debugParamDef(def->value.value.structval, level+1);
				break;
			case	arrayType:
				printDef("array", def);
				debugParamDef(def->value.value.arrayval, level+1);
				break;
			case	builtinType:
				printf("BUILTIN\n");
				break;
			default:
				fprintf(stderr,"Unknown value.type (%d)\n", def->value.type);
				exit(1);
		}
		def = def->next;
	}
}

void
dDump(ParamDef *def) {
	debugParamDef(def->value.value.structval, 0);
}
