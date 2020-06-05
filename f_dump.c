#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>

#include <prscfl.h>

void
printPrefix(FILE *fh, int level) {
	int i;

	for(i=0; i < level * 4; i++)
		fputc(' ', fh);
}

static void
dumpComment(FILE *fh, int level, ParamDef *def) {
	if (def->comment) {
		ParamDef	*i = def->comment;

		while(i) {
			printPrefix(fh, level);
			fprintf(fh, "# %s\n", i->value.value.commentval);
			i = i->next;
		}
	}
}

static void
dumpParamDef(FILE *fh, int level, ParamDef *def, bool inner) {
	while(def) {
		if (def->value.type == builtinType) {
			def = def->next;
			continue;
		}

		dumpComment(fh, level, def);

		printPrefix(fh, level);
		if (!def->parent || def->parent->value.type != arrayType)
			fprintf(fh, "%s = ", def->name);

		switch(def->value.type) {
			case	int32Type:
				fprintf(fh, "%"PRId32, def->value.value.int32val);
				break;
			case	uint32Type:
				fprintf(fh, "%"PRIu32, def->value.value.int32val);
				break;
			case	int64Type:
				fprintf(fh, "%"PRId64, def->value.value.int64val);
				break;
			case	uint64Type:
				fprintf(fh, "%"PRIu64, def->value.value.uint64val);
				break;
			case	doubleType:
				fprintf(fh, "%g", def->value.value.doubleval);
				break;
			case	stringType:
				if ( def->value.value.stringval) {
					char *ptr = def->value.value.stringval;
					fputc('\"', fh);

					while(*ptr) {
						if (*ptr == '"')
							fputc('\\', fh);
						fputc(*ptr, fh);
						ptr++;
					}

					fputc('\"', fh);
				} else {
					fputs("NULL", fh);
				}
				break;
			case	boolType:
				fprintf(fh, "%s", def->value.value.boolval ? "true" : "false");
				break;
			case	commentType:
				fprintf(stderr, "Unexpected comment");
				break;
			case	structType:
				fputs("{\n", fh);
				dumpParamDef(fh, level + 1, def->value.value.structval, inner);
				printPrefix(fh, level);
				fputs("}", fh);
				break;
			case	arrayType:
				fputs("[\n", fh);
				dumpParamDef(fh, level + 2, inner ? def->value.value.arrayval : def->value.value.arrayval->next, true);
				printPrefix(fh, level);
				fputs("]", fh);
				break;
			default:
				fprintf(stderr,"Unknown value.type (%d)\n", def->value.type);
				exit(1);
		}

		fputc('\n', fh);

		def = def->next;
	}
}

void
fDump(FILE *fh, ParamDef *def) {
	dumpParamDef(fh, 0, def->value.value.structval, false);
}
