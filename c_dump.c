#include <assert.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>

#include <prscfl.h>

static void
fputt(FILE *fh, int level) {
	while(level--)
		fputc('\t', fh);
}

static void
fputts(FILE *fh, int level, char *str) {
	while(level--)
		fputc('\t', fh);
	fputs(str, fh);
}

static void
dumpParamDefCName(FILE *fh, ParamDef *def) {
	fputs("_name__", fh);
	dumpStructName(fh, def, "__");
}

static int
dumpParamDefNameList(FILE *fh, ParamDef *def, ParamDef *odef, int level) {
    if (def && def->parent) {
			int maxlevel;

			maxlevel = dumpParamDefNameList(fh, def->parent, odef, level + 1);

			if (def->name)
				fprintf(fh,"\t{ \"%s\", -1, ", def->name);
			else
				fputs("\t{ NULL, -1, ", fh);

			if (level == 0) {
				fputs("NULL }\n", fh);
			} else {
				dumpParamDefCName(fh, odef);
				fprintf(fh, " + %d },\n", maxlevel - level);
			}

			return maxlevel;
	}

	return level;
}

static void
dumpParamDefCNameRecursive(FILE *fh, ParamDef *def) {
	while(def) {
		switch(def->value.type) {
			case	int32Type:
			case	uint32Type:
			case	int64Type:
			case	uint64Type:
			case	doubleType:
			case	stringType:
			case	boolType:
				fputs("static NameAtom ", fh);
				dumpParamDefCName(fh, def);
				fputs("[] = {\n", fh);
				dumpParamDefNameList(fh, def, def, 0);
				fputs("};\n", fh);
				break;
			case	commentType:
				fprintf(stderr, "Unexpected comment");
				break;
			case	structType:
				fputs("static NameAtom ", fh);
				dumpParamDefCName(fh, def);
				fputs("[] = {\n", fh);
				dumpParamDefNameList(fh, def, def, 0);
				fputs("};\n", fh);

				dumpParamDefCNameRecursive(fh, def->value.value.structval);
				break;
			case	arrayType:
				fputs("static NameAtom ", fh);
				dumpParamDefCName(fh, def);
				fputs("[] = {\n", fh);
				dumpParamDefNameList(fh, def, def, 0);
				fputs("};\n", fh);

				dumpParamDefCNameRecursive(fh, def->value.value.arrayval);
				break;
			case	builtinType:
				break;
			default:
				fprintf(stderr,"Unknown value.type (%d)\n", def->value.type);
				exit(1);
		}

		if (def->parent && def->parent->value.type == arrayType)
			return;

		def = def->next;
	}
}

static void
dumpArrayIndex(FILE *fh, int n) {
	fputs("opt->name", fh);
	while(n-- > 0)
		fputs("->next", fh);
	fputs("->index", fh);
}

static int
dumpStructFullPath(FILE *fh, char *name, char *itername, ParamDef *def, int isiterator, int show_index) {
	if (def->parent) {
		int n = dumpStructFullPath(fh, name, itername, def->parent, isiterator, show_index);

		if (def->parent->value.type == arrayType) {
			fputs(".val", fh);
			if (show_index) {
				fputs("[", fh);
				if (isiterator) {
					fprintf(fh, "%s->idx", itername);
					dumpParamDefCName(fh, def->parent);
				} else
					dumpArrayIndex(fh, n);
				fputs("]", fh);
			}
		} else {
				if (def->parent->parent)
					fprintf(fh, ".%s", def->name);
				else
					fprintf(fh, "->%s", def->name);
		}

		return n + 1;
	} else {
		fputs(name, fh);
		return 0;
	}
}

static uint32_t
arrayLen(ParamDef *def, bool template)
{
	uint32_t len = 0;

	while (def) {
		len++;
		def = def->next;
	}

	if (template)
		len--;

	return len;
}

static void
dumpStructPath(FILE *fh, ParamDef *def, char *name) {
	if (def->parent && def->parent->value.type == arrayType)
		fprintf(fh, "*%s", name);
	else
		fprintf(fh, "%s->%s", name, def->name);
}

static void
dumpDefault(FILE *fh, int level, ParamDef *def, bool template) {
	while(def) {
		switch (def->value.type) {
			case structType:
			case arrayType:
			case builtinType:
				break;
			default:
				fputt(fh, level);
				dumpStructPath(fh, def, "c");
		}

		switch(def->value.type) {
			case	int32Type:
				fprintf(fh, " = %"PRId32";\n", def->value.value.int32val);
				break;
			case	uint32Type:
				fprintf(fh, " = %"PRIu32"U;\n", def->value.value.uint32val);
				break;
			case	int64Type:
				fprintf(fh, " = %"PRId64"LL;\n", def->value.value.int64val);
				break;
			case	uint64Type:
				fprintf(fh, " = %"PRIu64"ULL;\n", def->value.value.uint64val);
				break;
			case	doubleType:
				fprintf(fh, " = %g;\n", def->value.value.doubleval);
				break;
			case	stringType:
				if (def->value.value.stringval == NULL) {
					fputs(" = NULL;\n", fh);
				} else {
					char *ptr = def->value.value.stringval;

					fputs(" = strdup(\"", fh);

					while(*ptr) {
						if (*ptr == '"')
							fputc('\\', fh);
						fputc(*ptr, fh);
						ptr++;
					}
					fputs("\");\n", fh);
					fputts(fh, level, "if (");
					dumpStructPath(fh, def, "c");
					fputs(" == NULL) return CNF_NOMEMORY;\n", fh);
				}
				break;
			case	boolType:
				fprintf(fh, " = %s;\n",
					    def->flags & PARAMDEF_REQUIRED ? "-1" : def->value.value.boolval ? "true" : "false");
				break;
			case	structType:
				if (def->parent) {
					fputts(fh, level, "if ((r = fill_default_");
					dumpStructName(fh, def, "_");
					if (def->name) {
						fprintf(fh, "(&c->%s, flags)) != CNF_OK)\n", def->name);
					} else
						fprintf(fh, "(c, flags)) != CNF_OK)\n");
					fputts(fh, level, "\treturn r;\n");
				} else
					dumpDefault(fh, level, def->value.value.structval, template);
				break;
			case	arrayType: {
				uint32_t n = arrayLen(def->value.value.arrayval, template);
				const char *name_fmt, *name;

				if (def->parent && def->parent->value.type == arrayType) {
					name_fmt = "c->%s";
					name = "";
				} else {
					name_fmt = "c->%s.";
					name = def->name;
				}

				fputt(fh, level);
				fprintf(fh, name_fmt, name);
				fputs("__confetti_flags = flags;\n", fh);
				if (n == 0) {
					fputt(fh, level);
					fprintf(fh, name_fmt, name);
					fputs("val = NULL;\n", fh);
					fputt(fh, level);
					fprintf(fh, name_fmt, name);
					fputs("n = 0;\n", fh);

					break;
				}

				fputt(fh, level);
				fprintf(fh, name_fmt, name);
				fprintf(fh, "val = malloc(%u * sizeof(*", n);
				fprintf(fh, name_fmt, name);
				fputs("val));\n", fh);
				fputts(fh, level, "if (");
				fprintf(fh, name_fmt, name);
				fputs("val == NULL) return CNF_NOMEMORY;\n", fh);
				fputt(fh, level);
				fprintf(fh, name_fmt, name);
				fprintf(fh, "n = %u;\n", n);
				fputts(fh, level, "{\n");
				fputt(fh, level + 1);
				dumpParamType(fh, def->value.value.arrayval);
				fputs(" *cc = ", fh);
				fprintf(fh, name_fmt, name);
				fputs("val;\n", fh);
				fputt(fh, level + 1);
				dumpParamType(fh, def->value.value.arrayval);
				fputs(" *c = cc;\n\n", fh);

				if (template)
					// the first array value is a template
					dumpDefault(fh, level + 1, def->value.value.arrayval->next, false);
				else
					dumpDefault(fh, level + 1, def->value.value.arrayval, true);

				fputts(fh, level, "}\n");
				break;
			}
			case	builtinType:
				break;
			case	commentType:
				fprintf(stderr, "Unexpected comment");
				break;
			default:
				fprintf(stderr,"Unknown value.type (%d)\n", def->value.type);
				exit(1);
		}

		if (def->parent && def->parent->value.type == arrayType)
				fputts(fh, level, "c++;\n");

		def = def->next;
	}
}

ParamDef *
childDef(ParamDef *def)
{
	switch (def->value.type) {
		case structType:
			return def->value.value.structval;
			break;
		case arrayType:
			return def->value.value.arrayval;
			break;
		default:
			abort();
	}

	return NULL;
}

static void
makeDefault(FILE *fh, ParamDef *def)
{
	ParamDef *child = childDef(def);

	while(child) {
		switch(child->value.type) {
			case	int32Type:
			case	uint32Type:
			case	int64Type:
			case	uint64Type:
			case	doubleType:
			case	boolType:
			case	commentType:
			case	stringType:
				break;
			case	arrayType:
			case	structType:
				makeDefault(fh, child);
				break;
			case	builtinType:
				break;
			default:
				fprintf(stderr,"Unknown value.type (%d)\n", def->value.type);
				exit(1);
		}

		if (def->value.type == arrayType)
			return;

		child = child->next;
	}

	if (def->parent != NULL)
		fputs("static ", fh);
	fputs(
		"int\n"
		"fill_default_",
		fh);
	dumpStructName(fh, def, "_");
	fputs("(", fh);
	dumpParamType(fh, def);
	fputs("* c, unsigned char flags) {\n"
		"\tint r = CNF_OK;\n\n"
		"\tc->__confetti_flags = flags;\n\n",
		fh);

	dumpDefault(fh, 1, childDef(def), true);

	fputs(
		"\n\treturn r;\n"
		"}\n\n",
		fh);
}

static void
dumpAcceptDefault(FILE *fh, ParamDef *def) {
	while (def) {
		switch(def->value.type) {
			case	structType:
				dumpAcceptDefault(fh, def->value.value.structval);
				break;
			case	arrayType: {
				ParamDef *tmp;

				dumpAcceptDefault(fh, def->value.value.arrayval);

				fputs(
				    "static int\n"
					"acceptDefault_",
					fh);
				dumpParamDefCName(fh, def->value.value.arrayval);
				fputs("(", fh);
				dumpParamType(fh, def->value.value.arrayval);
				fputs(" *c, unsigned char flags) {\n"
					"\tint r = CNF_OK;\n\n",
					fh);

				tmp = def->value.value.arrayval->next;
				def->value.value.arrayval->next = NULL;
				dumpDefault(fh, 1, def->value.value.arrayval, true);
				def->value.value.arrayval->next = tmp;

				fputs("\n\treturn r;\n"
					"}\n\n",
					fh);

				break;
			}
			default:
				break;
		}

		if (def->parent && def->parent->value.type == arrayType)
			break;

		def = def->next;
	}
}

static void
arrangeArray(FILE *fh, ParamDef *def) {
	if (!def->parent)
		return;
	arrangeArray(fh, def->parent);

	fputs("\t\t", fh);
	dumpStructFullPath(fh, "c", "i", def->parent, 0, 1);
	if (def->parent->parent)
		fputs(".__confetti_flags &= ~CNF_FLAG_STRUCT_NOTSET;\n", fh);
	else
		fputs("->__confetti_flags &= ~CNF_FLAG_STRUCT_NOTSET;\n", fh);

	if (def->value.type == arrayType) {
		int	n;

		fputs("\t\tARRAYALLOC(", fh);
		n = dumpStructFullPath(fh, "c", "i", def, 0, 1);
		fputs(", ", fh);
		dumpArrayIndex(fh, n);
		fputs(" + 1, ", fh);
		dumpParamDefCName(fh, def->value.value.arrayval);
		if (def->flags & PARAMDEF_RDONLY)
			fputs(", check_rdonly, 1, CNF_FLAG_STRUCT_NEW | CNF_FLAG_STRUCT_NOTSET);\n", fh);
		else
			fputs(", 0, 1, CNF_FLAG_STRUCT_NEW | CNF_FLAG_STRUCT_NOTSET);\n", fh);
		if (def->value.value.arrayval->value.type == structType) {
			fputs("\t\tif (", fh);
			dumpStructFullPath(fh, "c", "i", def, 0, 1);
			fputs(".val[", fh);
			dumpArrayIndex(fh, n);
			fputs("].__confetti_flags & CNF_FLAG_STRUCT_NEW)\n", fh);
			fputs("\t\t\tcheck_rdonly = 0;\n", fh);
		}
	}
}

static void
printIf(FILE *fh, ParamDef *def, int i) {
	if (i > 1)
		fputs("\telse ", fh);
	else
		fputs("\t", fh);

	fputs("if (cmpNameAtoms(opt->name, ", fh);
	dumpParamDefCName(fh, def);
	fputs(")) {\n", fh);
	fputs("\t\tif (opt->value.type != ", fh);
	switch(def->value.type) {
		case	int32Type:
		case	uint32Type:
		case	int64Type:
		case	uint64Type:
		case	doubleType:
		case	stringType:
		case	boolType:
			fputs("scalarType", fh);
			break;
		case	structType:
			fputs("structType", fh);
			break;
		case	arrayType:
			fputs("arrayType", fh);
			break;
		default:
			fprintf(stderr,"Unexpected def type: %d", def->value.type);
			break;
	}
	fputs(" )\n\t\t\treturn CNF_WRONGTYPE;\n", fh);

	if (def->value.type == structType ||
	    def->value.type == arrayType) {
		int n;
		int level = 2;

		if (def->parent->value.type == arrayType) {
			fputts(fh, level, "if (");
			n = dumpStructFullPath(fh, "c", "i", def->parent, 0, 1);
			fputs(".n > ", fh);
			dumpArrayIndex(fh, n);
			fputs(") {\n", fh);
			level++;
		}
			fputts(fh, level, "destroy_");
			dumpStructName(fh, def, "_");
			fputs("(&", fh);
			dumpStructFullPath(fh, "c", "i", def, 0, 1);
			fputs(");\n", fh);

			if (def->value.type == structType) {
				fputts(fh, level, "int r = fill_default_");
				dumpStructName(fh, def, "_");
				fputs("(&", fh);
				dumpStructFullPath(fh, "c", "i", def, 0, 1);
				fputs(", CNF_FLAG_STRUCT_NEW);\n", fh);
				fputts(fh, level, "if (r != CNF_OK)\n");
				fputts(fh, level, "\treturn r;\n");
			} else {
				fputt(fh, level);
				dumpStructFullPath(fh, "c", "i", def, 0, 1);
				fputs(".__confetti_flags = CNF_FLAG_STRUCT_NEW;\n", fh);
			}
		if (def->parent->value.type == arrayType) {
			level--;
			fputts(fh, level, "}\n");
		}
	}

	if (def->value.type == arrayType)
		arrangeArray(fh, def->parent);
	else
		arrangeArray(fh, def);
}

static int
makeAccept(FILE *fh, ParamDef *def, int i) {
	while(def) {
		switch(def->value.type) {
			case	int32Type:
			case	uint32Type:
			case	int64Type:
			case	uint64Type:
			case	doubleType:
			case	stringType:
			case	boolType:
				printIf(fh, def, i);
				fputs("\t\terrno = 0;\n", fh);
				switch(def->value.type) {
					case	int32Type:
						fputs("\t\tchar *endptr;\n", fh);
						fputs("\t\tlong int i32 = strtol(opt->value.value.scalarval, &endptr, 10);\n", fh);
						fputs("\t\tif (*endptr != '\\0')\n\t\t\treturn CNF_WRONGINT;\n", fh);
						fputs("\t\tif ((i32 == LONG_MIN || i32 == LONG_MAX) && errno == ERANGE)\n\t\t\treturn CNF_WRONGRANGE;\n", fh);
						if (def->flags & PARAMDEF_RDONLY) {
							fputs("\t\tif (check_rdonly && ", fh);
							dumpStructFullPath(fh, "c", "i", def, 0, 1);
							fputs(" != i32)\n\t\t\treturn CNF_RDONLY;\n", fh);
						}
						fputs("\t\t", fh);
							dumpStructFullPath(fh, "c", "i", def, 0, 1);
							fputs(" = i32;\n", fh);
						break;
					case	uint32Type:
						fputs("\t\tchar *endptr;\n", fh);
						fputs("\t\tunsigned long int u32 = strtoul(opt->value.value.scalarval, &endptr, 10);\n", fh);
						fputs("\t\tif (*endptr != '\\0')\n\t\t\treturn CNF_WRONGINT;\n", fh);
						fputs("\t\tif (u32 == ULONG_MAX && errno == ERANGE)\n\t\t\treturn CNF_WRONGRANGE;\n", fh);
						if (def->flags & PARAMDEF_RDONLY) {
							fputs("\t\tif (check_rdonly && ", fh);
							dumpStructFullPath(fh, "c", "i", def, 0, 1);
							fputs(" != u32)\n\t\t\treturn CNF_RDONLY;\n", fh);
						}
						fputs("\t\t", fh);
							dumpStructFullPath(fh, "c", "i", def, 0, 1);
							fputs(" = u32;\n", fh);
						break;
					case	int64Type:
						fputs("\t\tchar *endptr;\n", fh);
						fputs("\t\tlong long int i64 = strtoll(opt->value.value.scalarval, &endptr, 10);\n", fh);
						fputs("\t\tif (*endptr != '\\0')\n\t\t\treturn CNF_WRONGINT;\n", fh);
						fputs("\t\tif ((i64 == LLONG_MIN || i64 == LLONG_MAX) && errno == ERANGE)\n\t\t\treturn CNF_WRONGRANGE;\n", fh);
						if (def->flags & PARAMDEF_RDONLY) {
							fputs("\t\tif (check_rdonly && ", fh);
							dumpStructFullPath(fh, "c", "i", def, 0, 1);
							fputs(" != i64)\n\t\t\treturn CNF_RDONLY;\n", fh);
						}
						fputs("\t\t", fh);
							dumpStructFullPath(fh, "c", "i", def, 0, 1);
							fputs(" = i64;\n", fh);
						break;
					case	uint64Type:
						fputs("\t\tchar *endptr;\n", fh);
						fputs("\t\tunsigned long long int u64 = strtoull(opt->value.value.scalarval, &endptr, 10);\n", fh);
						fputs("\t\tif (*endptr != '\\0')\n\t\t\treturn CNF_WRONGINT;\n", fh);
						fputs("\t\tif (u64 == ULLONG_MAX && errno == ERANGE)\n\t\t\treturn CNF_WRONGRANGE;\n", fh);
						if (def->flags & PARAMDEF_RDONLY) {
							fputs("\t\tif (check_rdonly && ", fh);
							dumpStructFullPath(fh, "c", "i", def, 0, 1);
							fputs(" != u64)\n\t\t\treturn CNF_RDONLY;\n", fh);
						}
						fputs("\t\t", fh);
							dumpStructFullPath(fh, "c", "i", def, 0, 1);
							fputs(" = u64;\n", fh);
						break;
					case	doubleType:
						fputs("\t\tchar *endptr;\n", fh);
						fputs("\t\tdouble dbl = strtod(opt->value.value.scalarval, &endptr);\n", fh);
						fputs("\t\tif (*endptr != '\\0')\n\t\t\treturn CNF_WRONGDOUBLE;\n", fh);
						fputs("\t\tif ((dbl == 0 || dbl == -HUGE_VAL || dbl == HUGE_VAL) && errno == ERANGE)\n\t\t\treturn CNF_WRONGRANGE;\n", fh);
						if (def->flags & PARAMDEF_RDONLY) {
							fputs("\t\tif (check_rdonly && ", fh);
							dumpStructFullPath(fh, "c", "i", def, 0, 1);
							fputs(" != dbl)\n\t\t\treturn CNF_RDONLY;\n", fh);
						}
						fputs("\t\t", fh);
							dumpStructFullPath(fh, "c", "i", def, 0, 1);
							fputs(" = dbl;\n", fh);
						break;
					case	stringType:
						if (def->flags & PARAMDEF_RDONLY) {
							fputs("\t\tif (check_rdonly && ((opt->value.value.scalarval == NULL && ", fh);
							dumpStructFullPath(fh, "c", "i", def, 0, 1);
							fputs(" == NULL) || confetti_strcmp(opt->value.value.scalarval, ", fh);
							dumpStructFullPath(fh, "c", "i", def, 0, 1);
							fputs(") != 0))\n\t\t\treturn CNF_RDONLY;\n", fh);
						}
						fputs("\t\tif (", fh);
							dumpStructFullPath(fh, "c", "i", def, 0, 1);
						fputs(") free(", fh);
							dumpStructFullPath(fh, "c", "i", def, 0, 1);
						fputs(");\n", fh);
						fputs("\t\t", fh);
							dumpStructFullPath(fh, "c", "i", def, 0, 1);
							fputs(" = (opt->value.value.scalarval) ? strdup(opt->value.value.scalarval) : NULL;\n", fh);
						fputs("\t\tif (opt->value.value.scalarval && ", fh);
							dumpStructFullPath(fh, "c", "i", def, 0, 1);
							fputs(" == NULL)\n\t\t\treturn CNF_NOMEMORY;\n", fh);
						break;
					case	boolType:
						fputs("\t\tbool bln;\n\n", fh);
						fputs("\t\tif (strcasecmp(opt->value.value.scalarval, \"true\") == 0 ||\n", fh);
						fputs("\t\t\t\tstrcasecmp(opt->value.value.scalarval, \"yes\") == 0 ||\n", fh);
						fputs("\t\t\t\tstrcasecmp(opt->value.value.scalarval, \"enable\") == 0 ||\n", fh);
						fputs("\t\t\t\tstrcasecmp(opt->value.value.scalarval, \"on\") == 0 ||\n", fh);
						fputs("\t\t\t\tstrcasecmp(opt->value.value.scalarval, \"1\") == 0 )\n", fh);
						fputs("\t\t\tbln = true;\n", fh);
						fputs("\t\telse if (strcasecmp(opt->value.value.scalarval, \"false\") == 0 ||\n", fh);
						fputs("\t\t\t\tstrcasecmp(opt->value.value.scalarval, \"no\") == 0 ||\n", fh);
						fputs("\t\t\t\tstrcasecmp(opt->value.value.scalarval, \"disable\") == 0 ||\n", fh);
						fputs("\t\t\t\tstrcasecmp(opt->value.value.scalarval, \"off\") == 0 ||\n", fh);
						fputs("\t\t\t\tstrcasecmp(opt->value.value.scalarval, \"0\") == 0 )\n", fh);
						fputs("\t\t\tbln = false;\n", fh);
						fputs("\t\telse\n", fh);
						fputs("\t\t\treturn CNF_WRONGRANGE;\n", fh);

						if (def->flags & PARAMDEF_RDONLY) {
							fputs("\t\tif (check_rdonly && ", fh);
							dumpStructFullPath(fh, "c", "i", def, 0, 1);
							fputs(" != bln)\n\t\t\treturn CNF_RDONLY;\n", fh);
						}
						fputs("\t\t", fh);
							dumpStructFullPath(fh, "c", "i", def, 0, 1);
							fputs(" = bln;\n", fh);
						break;
					default:
						break;
				}
				fputs("\t}\n",fh);
				break;
			case	commentType:
				fprintf(stderr, "Unexpected comment");
				break;
			case	structType:
				if (def->parent) {
					printIf(fh, def, i);
					fputs("\t}\n",fh);
				}
				i = makeAccept(fh, def->value.value.structval, i);
				break;
			case	arrayType:
				printIf(fh, def, i);
				fputs("\t}\n",fh);
				i = makeAccept(fh, def->value.value.arrayval, i);
				break;
			case	builtinType:
				break;
			default:
				fprintf(stderr,"Unknown value.type (%d)\n", def->value.type);
				exit(1);
		}

		if (def->parent && def->parent->value.type == arrayType)
			return i;

		i++;
		def = def->next;
	}

	return i;
}

static void
makeIteratorStates(FILE *fh, ParamDef *def) {
	while(def) {
		switch(def->value.type) {
			case	int32Type:
			case	uint32Type:
			case	int64Type:
			case	uint64Type:
			case	doubleType:
			case	stringType:
			case	boolType:
				fputs("\tS", fh);
				dumpParamDefCName(fh, def);
				fputs(",\n", fh);
				break;
			case	commentType:
				fprintf(stderr, "Unexpected comment");
				break;
			case	structType:
				fputs("\tS", fh);
				dumpParamDefCName(fh, def);
				fputs(",\n", fh);
				makeIteratorStates(fh, def->value.value.structval);
				break;
			case	arrayType:
				fputs("\tS", fh);
				dumpParamDefCName(fh, def);
				fputs(",\n", fh);
				makeIteratorStates(fh, def->value.value.arrayval);
				break;
			case	builtinType:
				break;
			default:
				fprintf(stderr,"Unknown value.type (%d)\n", def->value.type);
				exit(1);
		}

		if (!def->parent || def->parent->value.type == arrayType)
			return;

		def = def->next;
	}
}

static void
makeArrayIndexes(FILE *fh, ParamDef *def) {
	while(def) {
		switch(def->value.type) {
			case	int32Type:
			case	uint32Type:
			case	int64Type:
			case	uint64Type:
			case	doubleType:
			case	stringType:
			case	boolType:
				break;
			case	commentType:
				fprintf(stderr, "Unexpected comment");
				break;
			case	structType:
				makeArrayIndexes(fh, def->value.value.structval);
				break;
			case	arrayType:
				fputs("\tint\tidx", fh);
				dumpParamDefCName(fh, def);
				fputs(";\n", fh);
				makeArrayIndexes(fh, def->value.value.arrayval);
				break;
			case	builtinType:
				break;
			default:
				fprintf(stderr,"Unknown value.type (%d)\n", def->value.type);
				exit(1);
		}

		if (!def->parent || def->parent->value.type == arrayType)
			return;

		def = def->next;
	}
}

static void
strdupValue(FILE *fh, ParamDef *def, int level) {
	switch(def->value.type) {
		case	int32Type:
			fputt(fh, level); fputs("*v = malloc(32);\n", fh);
			fputt(fh, level); fputs("if (*v == NULL) {\n", fh);
			fputt(fh, level+1); fputs("free(i);\n",fh);
			fputt(fh, level+1); fputs("out_warning(CNF_NOMEMORY, \"No memory to output value\");\n", fh);
			fputt(fh, level+1); fputs("return NULL;\n",fh);
			fputt(fh, level); fputs("}\n",fh);
			fputt(fh, level); fputs("sprintf(*v, \"%\"PRId32, ", fh);
				dumpStructFullPath(fh, "c", "i", def, 1, 1);
				fputs(");\n", fh);
			break;
		case	uint32Type:
			fputt(fh, level); fputs("*v = malloc(32);\n", fh);
			fputt(fh, level); fputs("if (*v == NULL) {\n", fh);
			fputt(fh, level+1); fputs("free(i);\n",fh);
			fputt(fh, level+1); fputs("out_warning(CNF_NOMEMORY, \"No memory to output value\");\n", fh);
			fputt(fh, level+1); fputs("return NULL;\n",fh);
			fputt(fh, level); fputs("}\n",fh);
			fputt(fh, level); fputs("sprintf(*v, \"%\"PRIu32, ", fh);
				dumpStructFullPath(fh, "c", "i", def, 1, 1);
				fputs(");\n", fh);
			break;
		case	int64Type:
			fputt(fh, level); fputs("*v = malloc(32);\n", fh);
			fputt(fh, level); fputs("if (*v == NULL) {\n", fh);
			fputt(fh, level+1); fputs("free(i);\n",fh);
			fputt(fh, level+1); fputs("out_warning(CNF_NOMEMORY, \"No memory to output value\");\n", fh);
			fputt(fh, level+1); fputs("return NULL;\n",fh);
			fputt(fh, level); fputs("}\n",fh);
			fputt(fh, level); fputs("sprintf(*v, \"%\"PRId64, ", fh);
				dumpStructFullPath(fh, "c", "i", def, 1, 1);
				fputs(");\n", fh);
			break;
		case	uint64Type:
			fputt(fh, level); fputs("*v = malloc(32);\n", fh);
			fputt(fh, level); fputs("if (*v == NULL) {\n", fh);
			fputt(fh, level+1); fputs("free(i);\n",fh);
			fputt(fh, level+1); fputs("out_warning(CNF_NOMEMORY, \"No memory to output value\");\n", fh);
			fputt(fh, level+1); fputs("return NULL;\n",fh);
			fputt(fh, level); fputs("}\n",fh);
			fputt(fh, level); fputs("sprintf(*v, \"%\"PRIu64, ", fh);
				dumpStructFullPath(fh, "c", "i", def, 1, 1);
				fputs(");\n", fh);
			break;
		case	doubleType:
			fputt(fh, level); fputs("*v = malloc(32);\n", fh);
			fputt(fh, level); fputs("if (*v == NULL) {\n", fh);
			fputt(fh, level+1); fputs("free(i);\n",fh);
			fputt(fh, level+1); fputs("out_warning(CNF_NOMEMORY, \"No memory to output value\");\n", fh);
			fputt(fh, level+1); fputs("return NULL;\n",fh);
			fputt(fh, level); fputs("}\n",fh);
			fputt(fh, level); fputs("sprintf(*v, \"%g\", ", fh);
				dumpStructFullPath(fh, "c", "i", def, 1, 1);
				fputs(");\n", fh);
			break;
		case	stringType:
			fputt(fh, level);
			fputs("*v = (", fh);
				dumpStructFullPath(fh, "c", "i", def, 1, 1);
				fputs(") ? strdup(", fh);
				dumpStructFullPath(fh, "c", "i", def, 1, 1);
				fputs(") : NULL;\n", fh);
			fputt(fh, level); fputs("if (*v == NULL && ", fh);
				dumpStructFullPath(fh, "c", "i", def, 1, 1);
				fputs(") {\n", fh);
			fputt(fh, level+1); fputs("free(i);\n",fh);
			fputt(fh, level+1); fputs("out_warning(CNF_NOMEMORY, \"No memory to output value\");\n", fh);
			fputt(fh, level+1); fputs("return NULL;\n",fh);
			fputt(fh, level); fputs("}\n",fh);
			break;
		case	boolType:
			fputt(fh, level); fputs("*v = malloc(8);\n", fh);
			fputt(fh, level); fputs("if (*v == NULL) {\n", fh);
			fputt(fh, level+1); fputs("free(i);\n",fh);
			fputt(fh, level+1); fputs("out_warning(CNF_NOMEMORY, \"No memory to output value\");\n", fh);
			fputt(fh, level+1); fputs("return NULL;\n",fh);
			fputt(fh, level); fputs("}\n",fh);
			fputt(fh, level); fputs("sprintf(*v, \"%s\", ", fh);
				if (def->flags & PARAMDEF_REQUIRED) {
					dumpStructFullPath(fh, "c", "i", def, 1, 1);
					fputs(" == -1 ? ", fh);
					fputs(def->value.value.boolval ? "\"true\"" : "\"false\"", fh);
					fputs(" : ", fh);
				}
				dumpStructFullPath(fh, "c", "i", def, 1, 1);
				fputs(" ? \"true\" : \"false\");\n", fh);
			break;
		default:
			break;
	}
}

static void
resetSubArray(FILE *fh, ParamDef *def, int level) {
	while(def) {
		switch(def->value.type) {
			case structType:
				resetSubArray(fh, def->value.value.structval, level);
				break;
			case arrayType:
				fputt(fh, level);
				fputs("i->idx", fh);
				dumpParamDefCName(fh, def);
				fputs(" = 0;\n", fh);
				resetSubArray(fh, def->value.value.arrayval, level);
				break;
			default:
				break;
		}

		def = def->next;
	}
}

static int
dumpStructNameFullPath(FILE *fh, ParamDef *def, int innerCall) {
	if (def->parent) {
		int n;

		n = dumpStructNameFullPath(fh, def->parent, 1);

		if (n != 0 && def->name)
			fputs(".", fh);

		if (def->name)
			fputs(def->name, fh);

		if (def->value.type == arrayType)
			fputs("[%d]", fh);

		return n + 1;
	} else {
		return 0;
	}
}

static int
dumpArrayIndexes(FILE *fh, ParamDef *def, int innerCall) {
	if (def) {
		int n;

		n = dumpArrayIndexes(fh, def->parent, 1);
		if (def->value.type == arrayType && innerCall) {
			fputs(", ", fh);
			fputs("i->idx", fh);
			dumpParamDefCName(fh, def);
		}
		return n + 1;
	} else {
		return 0;
	}
}

static void
dumpStateNext(FILE *fh, ParamDef *def, int level, ParamDef *next, ParamDef *array)
{
	fputt(fh, level);
	if (next || def->next) {
		fputs("i->state = S", fh);
		dumpParamDefCName(fh, def->next ? def->next : next);
	} else
		fputs("i->state = _S_Finished", fh);
	fputs(";\n", fh);

	/* extra work to switch to the next state */
	if (!def->next && array && array == next) {
			fputt(fh, level);
			fputs("i->idx", fh);
			dumpParamDefCName(fh, next);
			fputs("++;\n", fh);
			resetSubArray(fh, next->value.value.arrayval, level);
	}
}

static void
makeSwitch(FILE *fh, ParamDef *def, int level, ParamDef *next, ParamDef *array) {
	while(def) {
		switch(def->value.type) {
			case	int32Type:
			case	uint32Type:
			case	int64Type:
			case	uint64Type:
			case	doubleType:
			case	stringType:
			case	boolType:
				/* case */
				fputt(fh, level); fputs( "case S", fh);
				dumpParamDefCName(fh, def);
				fputs(":\n", fh);
				/* make val */
				strdupValue(fh, def, level + 1);
				/* make name */
				fputt(fh, level);
				fputs("\tsnprintf(buf, PRINTBUFLEN - 1, \"", fh);
				dumpStructNameFullPath(fh, def, 0);
				fputs("\"", fh);
				dumpArrayIndexes(fh, def, 0);
				fputs(");\n", fh);
				dumpStateNext(fh, def, level + 1, next, array);
				fputt(fh, level);
				fputs("\treturn buf;\n", fh);
				break;
			case	commentType:
				fprintf(stderr, "Unexpected comment");
				break;
			case	structType:
				fputt(fh, level); fputs( "case S", fh);
				dumpParamDefCName(fh, def);
				fputs(":\n", fh);
				makeSwitch(fh, def->value.value.structval, level, def->next ? def->next : next, array);
				break;
			case	arrayType: {
				ParamDef *tmp;

				fputt(fh, level); fputs( "case S", fh);
				dumpParamDefCName(fh, def);
				fputs(":\n", fh);
					fputt(fh, level);
					fputs("\tif (", fh);
					fputs("i->idx", fh);
					dumpParamDefCName(fh, def);
					fputs(" < ", fh);
					dumpStructFullPath(fh, "c", "i", def, 1, 1);
					fputs(".n)\n", fh);
						fputt(fh, level);
						fputs( "\t\ti->state = S", fh);
						dumpParamDefCName(fh, def->value.value.arrayval);
						fputs(";\n", fh);
					fputt(fh, level);
					fputs("\telse {\n", fh);
						dumpStateNext(fh, def, level + 2, next, array);
					fputts(fh, level, "\t}\n");
					fputts(fh, level, "\tgoto again;\n");

					tmp = def->value.value.arrayval->next;
					def->value.value.arrayval->next = NULL;
					makeSwitch(fh, def->value.value.arrayval, level, def, def);
					def->value.value.arrayval->next = tmp;
				break;
			}
			case	builtinType:
				break;
			default:
				fprintf(stderr,"Unknown value.type (%d)\n", def->value.type);
				exit(1);
		}

		if (!def->parent || def->parent->value.type == arrayType)
			return;

		def = def->next;
	}
}

static int
countParents(ParamDef *def) {
	int n = 0;

	while (def && def->parent) {
		n++;
		def = def->parent;
	}

	return n;
}

static void
dumpCheckArrayIndexes(FILE *fh, ParamDef *def, int level) {
	ParamDef	*parent = def->parent;
	int			i, n;

	for (parent = def->parent; parent && parent->parent; parent = parent->parent) {
		if (parent->name == NULL)
			continue;

		if (parent->value.type == arrayType) {
			fputt(fh, level);
			dumpParamDefCName(fh, def);

			n = countParents(parent->parent);
			for(i = 0; i < n; i++)
				fputs("->next", fh);

			fputs("->index = i->idx", fh);
			dumpParamDefCName(fh, parent);
			fputs(";\n", fh);
		}
	}
}

static void
makeOutCheck(FILE *fh, ParamDef *def, int level) {
	fputt(fh, level);
	fputs("res++;\n", fh);
	dumpCheckArrayIndexes(fh, def, level);
	fputt(fh, level);
	fputs("out_warning(CNF_NOTSET, \"Option '%s' is not set (or has a default value)\", dumpOptDef(", fh);
	dumpParamDefCName(fh, def);
	fputs("));\n", fh);
}

static void
makeCheck(FILE *fh, ParamDef *def, int level) {
	while(def) {
		switch(def->value.type) {
			case	int32Type:
				if ((def->flags & PARAMDEF_REQUIRED) == 0)
					break;
				fputt(fh, level);
				fputs("if (", fh);
				dumpStructFullPath(fh, "c", "i", def, 1, 1);
				fprintf(fh, " == %"PRId32") {\n", def->value.value.int32val);
				makeOutCheck(fh, def, level + 1);
				fputts(fh, level, "}\n");
				break;
			case	uint32Type:
				if ((def->flags & PARAMDEF_REQUIRED) == 0)
					break;
				fputt(fh, level);
				fputs("if (", fh);
				dumpStructFullPath(fh, "c", "i", def, 1, 1);
				fprintf(fh, " == %"PRIu32") {\n", def->value.value.uint32val);
				makeOutCheck(fh, def, level + 1);
				fputts(fh, level, "}\n");
				break;
			case	int64Type:
				if ((def->flags & PARAMDEF_REQUIRED) == 0)
					break;
				fputt(fh, level);
				fputs("if (", fh);
				dumpStructFullPath(fh, "c", "i", def, 1, 1);
				fprintf(fh, " == %"PRId64") {\n", def->value.value.int64val);
				makeOutCheck(fh, def, level + 1);
				fputts(fh, level, "}\n");
				break;
			case	uint64Type:
				if ((def->flags & PARAMDEF_REQUIRED) == 0)
					break;
				fputt(fh, level);
				fputs("if (", fh);
				dumpStructFullPath(fh, "c", "i", def, 1, 1);
				fprintf(fh, " == %"PRIu64") {\n", def->value.value.uint64val);
				makeOutCheck(fh, def, level + 1);
				fputts(fh, level, "}\n");
				break;
			case	doubleType:
				if ((def->flags & PARAMDEF_REQUIRED) == 0)
					break;
				fputt(fh, level);
				fputs("if (", fh);
				dumpStructFullPath(fh, "c", "i", def, 1, 1);
				fprintf(fh, " == %g) {\n", def->value.value.doubleval);
				makeOutCheck(fh, def, level + 1);
				fputts(fh, level, "}\n");
				break;
			case	stringType:
				if ((def->flags & PARAMDEF_REQUIRED) == 0)
					break;
				fputt(fh, level);
				if (def->value.value.stringval == NULL) {
					fputs("if (", fh);
					dumpStructFullPath(fh, "c", "i", def, 1, 1);
					fputs(" == NULL) {\n", fh);
				} else {
					char *ptr = def->value.value.stringval;

					fputs("if (", fh);
					dumpStructFullPath(fh, "c", "i", def, 1, 1);
					fputs(" != NULL && strcmp(", fh);
					dumpStructFullPath(fh, "c", "i", def, 1, 1);
					fputs(", \"", fh);

					while(*ptr) {
						if (*ptr == '"')
							fputc('\\', fh);
						fputc(*ptr, fh);
						ptr++;
					}
					fputs("\") == 0) {\n", fh);
				}
				makeOutCheck(fh, def, level + 1);
				fputts(fh, level, "}\n");
				break;
			case	boolType:
				if ((def->flags & PARAMDEF_REQUIRED) == 0)
					break;
				fputt(fh, level);
				fputs("if (", fh);
				dumpStructFullPath(fh, "c", "i", def, 1, 1);
				fputs(" == -1) {\n", fh);
				makeOutCheck(fh, def, level + 1);
				fputts(fh, level, "}\n");
				break;
			case	commentType:
				fprintf(stderr, "Unexpected comment");
				break;
			case	structType:
				fputs("\n", fh);
				if (def->flags & PARAMDEF_REQUIRED) {
					fputts(fh, level, "if (");
					dumpStructFullPath(fh, "c", "i", def, 1, 1);
					if (def->parent)
						fputs(".__confetti_flags & CNF_FLAG_STRUCT_NOTSET) {\n", fh);
					else
						fputs("->__confetti_flags & CNF_FLAG_STRUCT_NOTSET) {\n", fh);
					fputts(fh, level, "\tres++;\n");
					dumpCheckArrayIndexes(fh, def, level + 1);
					fputts(fh, level, "\tout_warning(CNF_NOTSET, \"Option '%s' is not set\", dumpOptDef(");
					dumpParamDefCName(fh, def);
					fputs("));\n", fh);
					fputts(fh, level, "}\n");
				}
				makeCheck(fh, def->value.value.structval, level);
				break;
			case	arrayType:
				fputs("\n", fh);
				if (def->flags & PARAMDEF_REQUIRED) {
					fputts(fh, level, "if (");
					dumpStructFullPath(fh, "c", "i", def, 1, 1);
					fputs(".__confetti_flags & CNF_FLAG_STRUCT_NOTSET) {\n", fh);
					fputts(fh, level, "\tres++;\n");
					dumpCheckArrayIndexes(fh, def, level + 1);
					fputts(fh, level, "\tout_warning(CNF_NOTSET, \"Option '%s' is not set\", dumpOptDef(");
					dumpParamDefCName(fh, def);
					fputs("));\n", fh);
					fputts(fh, level, "}\n");
				}
				fputt(fh, level);
				fputs("i->idx", fh);
				dumpParamDefCName(fh, def);
				fputs(" = 0;\n", fh);
				fputt(fh, level);
				fputs("while (", fh);
				fputs("i->idx", fh);
				dumpParamDefCName(fh, def);
				fputs(" < ", fh);
				dumpStructFullPath(fh, "c", "i", def, 1, 1);
				fputs(".n) {\n", fh);
				makeCheck(fh, def->value.value.arrayval, level + 1);
				fputts(fh, level, "\ti->idx");
				dumpParamDefCName(fh, def);
				fputs("++;\n", fh);
				fputts(fh, level, "}\n");
				break;
			case	builtinType:
				break;
			default:
				fprintf(stderr,"Unknown value.type (%d)\n", def->value.type);
				exit(1);
		}
		def = def->next;
	}
}

static void
makeCleanFlags(FILE *fh, ParamDef *def, int level) {
	while(def) {
		switch(def->value.type) {
			case	int32Type:
			case	uint32Type:
			case	int64Type:
			case	uint64Type:
			case	doubleType:
			case	stringType:
			case	boolType:
				break;
			case	commentType:
				break;
			case	structType:
				fputt(fh, level);
				dumpStructFullPath(fh, "c", "i", def, 1, 1);
				if (def->parent)
					fputs(".__confetti_flags &= ~CNF_FLAG_STRUCT_NEW;\n", fh);
				else
					fputs("->__confetti_flags &= ~CNF_FLAG_STRUCT_NEW;\n", fh);
				makeCleanFlags(fh, def->value.value.structval, level);
				break;
			case	arrayType:
				fputs("\n", fh);
				fputt(fh, level);
				dumpStructFullPath(fh, "c", "i", def, 1, 1);
				fputs(".__confetti_flags &= ~CNF_FLAG_STRUCT_NEW;\n", fh);
				fputts(fh, level, "i->idx");
				dumpParamDefCName(fh, def);
				fputs(" = 0;\n", fh);
				fputts(fh, level, "while (");
				fputs("i->idx", fh);
				dumpParamDefCName(fh, def);
				fputs(" < ", fh);
				dumpStructFullPath(fh, "c", "i", def, 1, 1);
				fputs(".n) {\n", fh);
					makeCleanFlags(fh, def->value.value.arrayval, level + 1);
					fputs("\n", fh);
					fputts(fh, level, "\ti->idx");
					dumpParamDefCName(fh, def);
					fputs("++;\n", fh);
				fputts(fh, level, "}\n");
				break;
			case	builtinType:
				break;
			default:
				fprintf(stderr,"Unknown value.type (%d)\n", def->value.type);
				exit(1);
		}

		if (!def->parent || def->parent->value.type == arrayType)
			return;

		def = def->next;
	}
}

static void
makeDup(FILE *fh, ParamDef *def, int level) {
	while(def) {
		switch(def->value.type) {
			case	int32Type:
			case	uint32Type:
			case	int64Type:
			case	uint64Type:
			case	doubleType:
			case	boolType:
				fputt(fh, level);
				dumpStructFullPath(fh, "dst", "i", def, 1, 1);
				fputs(" = ", fh);
				dumpStructFullPath(fh, "src", "i", def, 1, 1);
				fputs(";\n", fh);
				break;
			case	stringType:
				fputt(fh, level);

				fputs("if (", fh);
				dumpStructFullPath(fh, "dst", "i", def, 1, 1);
				fputs(")\n", fh);
				fputts(fh, level, "\tfree(");
				dumpStructFullPath(fh, "dst", "i", def, 1, 1);
				fputs(");\n", fh);

				fputt(fh, level);
				dumpStructFullPath(fh, "dst", "i", def, 1, 1);
				fputs(" = ", fh);
				dumpStructFullPath(fh, "src", "i", def, 1, 1);
				fputs(" == NULL ? NULL : strdup(", fh);
				dumpStructFullPath(fh, "src", "i", def, 1, 1);
				fputs(");\n", fh);
				fputts(fh, level, "if (");
				dumpStructFullPath(fh, "src", "i", def, 1, 1);
				fputs(" != NULL && ", fh);
				dumpStructFullPath(fh, "dst", "i", def, 1, 1);
				fputs(" == NULL)\n", fh);
					fputts(fh, level, "\treturn CNF_NOMEMORY;\n");
				break;
			case	commentType:
				break;
			case	structType:
				fputs("\n", fh);
				fputt(fh, level);
				dumpStructFullPath(fh, "dst", "i", def, 1, 1);
				if (def->parent)
					fputs(".__confetti_flags = ", fh);
				else
					fputs("->__confetti_flags = ", fh);
				dumpStructFullPath(fh, "src", "i", def, 1, 1);
				if (def->parent)
					fputs(".__confetti_flags;\n", fh);
				else
					fputs("->__confetti_flags;\n", fh);
				makeDup(fh, def->value.value.structval, level);
				break;
			case	arrayType:
				fputs("\n", fh);
				fputt(fh, level);
				dumpStructFullPath(fh, "dst", "i", def, 1, 1);
				fputs(".__confetti_flags = ", fh);
				dumpStructFullPath(fh, "src", "i", def, 1, 1);
				fputs(".__confetti_flags;\n", fh);
				fputts(fh, level, "if (");
				dumpStructFullPath(fh, "src", "i", def, 1, 1);
				fputs(".n != 0) {\n", fh);
					fputts(fh, level, "\ti->idx");
					dumpParamDefCName(fh, def);
					fputs(" = 0;\n", fh);
					fputts(fh, level, "\tARRAYALLOC(");
					dumpStructFullPath(fh, "dst", "i", def, 1, 1);
					fputs(", ", fh);
					dumpStructFullPath(fh, "src", "i", def, 1, 1);
					fputs(".n, ", fh);
					dumpParamDefCName(fh, def->value.value.arrayval);
					fputs(", 0, 0, 0);\n\n", fh);
					fputts(fh, level, "\twhile (i->idx");
					dumpParamDefCName(fh, def);
					fputs(" < ", fh);
					dumpStructFullPath(fh, "src", "i", def, 1, 1);
					fputs(".n) {\n", fh);
						makeDup(fh, def->value.value.arrayval, level + 2);
						fputs("\n", fh);
						fputts(fh, level, "\t\ti->idx");
						dumpParamDefCName(fh, def);
						fputs("++;\n", fh);
					fputts(fh, level, "\t}\n");
					fputts(fh, level, "\t");
					dumpStructFullPath(fh, "dst", "i", def, 1, 1);
					fputs(".n = ", fh);
					dumpStructFullPath(fh, "src", "i", def, 1, 1);
					fputs(".n;\n", fh);
				fputts(fh, level, "} else {\n");
					fputt(fh, level + 1);
					dumpStructFullPath(fh, "dst", "i", def, 1, 1);
					fputs(".n = 0;\n", fh);
					fputt(fh, level + 1);
					dumpStructFullPath(fh, "dst", "i", def, 1, 1);
					fputs(".val = NULL;\n", fh);
				fputts(fh, level, "}\n");
				break;
			case	builtinType:
				break;
			default:
				fprintf(stderr,"Unknown value.type (%d)\n", def->value.type);
				exit(1);
		}

		if (def->parent && def->parent->value.type == arrayType)
			return;

		def = def->next;
	}
}

static void
makeDestroy(FILE *fh, ParamDef *def) {
	ParamDef *child = childDef(def);
	int level = 1;

	while(child) {
		switch(child->value.type) {
			case	int32Type:
			case	uint32Type:
			case	int64Type:
			case	uint64Type:
			case	doubleType:
			case	boolType:
			case	commentType:
			case	stringType:
				break;
			case	structType:
			case	arrayType:
				makeDestroy(fh, child);
				break;
			case	builtinType:
				break;
			default:
				fprintf(stderr,"Unknown value.type (%d)\n", def->value.type);
				exit(1);
		}

		if (def->value.type == arrayType)
			break;

		child = child->next;
	}

	if (def->parent != NULL)
		fputs("static ", fh);
	fputs(
		"void\n"
		"destroy_",
		fh);
	dumpStructName(fh, def, "_");
	fputs("(", fh);
	dumpParamType(fh, def);
	fputs("* c) {\n", fh);

	if (def->value.type == arrayType) {
		fputts(fh, level, "int i;\n\n");
		fputts(fh, level, "for (i = 0; i < c->n; i++) {\n");
		fputts(fh, level, "\t__typeof__(c->val[0]) *cc = &c->val[i];\n");
		fputts(fh, level, "\t__typeof__(c->val[0]) *c = cc;\n\n");
		fputts(fh, level, "\t(void)c;\n");
		level++;
	}

	child = childDef(def);
	while (child) {
		switch(child->value.type) {
			case	int32Type:
			case	uint32Type:
			case	int64Type:
			case	uint64Type:
			case	doubleType:
			case	boolType:
			case	commentType:
				break;
			case	stringType:
				fputts(fh, level, "if (");
				dumpStructPath(fh, child, "c");
				fputs(" != NULL)\n", fh);
				fputts(fh, level, "\tfree(");
				dumpStructPath(fh, child, "c");
				fputs(");\n", fh);
				break;
			case	structType:
			case	arrayType:
				fputts(fh, level, "destroy_");
				dumpStructName(fh, child, "_");
				if (def->value.type == structType)
					fprintf(fh, "(&c->%s);\n", child->name);
				else
					fputs("(c);\n", fh);
				break;
			case	builtinType:
				break;
			default:
				fprintf(stderr,"Unknown value.type (%d)\n", def->value.type);
				exit(1);
		}

		if (def->value.type == arrayType)
			break;

		child = child->next;
	}

	if (def->value.type == arrayType) {
		level--;
		fputts(fh, level, "}\n\n");

		fputts(fh, level, "free(c->val);\n");
		fputts(fh, level, "c->val = NULL;\n");
		fputts(fh, level, "c->n = 0;\n");
	}

	fputs("}\n\n", fh);
}

static void
makeCmp(FILE *fh, ParamDef *def, int level) {
	while(def) {
		switch(def->value.type) {
			case	int32Type:
			case	uint32Type:
			case	int64Type:
			case	uint64Type:
			case	doubleType:
			case	boolType:
				if (!(def->flags & PARAMDEF_RDONLY)) {
					fputts(fh, level, "if (!only_check_rdonly) {\n");
					level++;
				}

				fputts(fh, level, "if (");
				dumpStructFullPath(fh, "c1", "i1", def, 1, 1);
				fputs(" != ", fh);
				dumpStructFullPath(fh, "c2", "i2", def, 1, 1);
				fputs(") {\n", fh);
					fputts(fh, level, "\tsnprintf(diff, PRINTBUFLEN - 1, \"%s\", \"");
					dumpStructFullPath(fh, "c", "i", def, 1, 0);
					fputs("\");\n\n", fh);
					fputts(fh, level, "\treturn diff;\n");
				fputts(fh, level, "}\n");

				if (!(def->flags & PARAMDEF_RDONLY)) {
					level--;
					fputts(fh, level, "}\n");
				}
				break;
			case	commentType:
				break;
			case	stringType:
				if (!(def->flags & PARAMDEF_RDONLY)) {
					fputts(fh, level, "if (!only_check_rdonly) {\n");
					level++;
				}

				fputts(fh, level, "if (confetti_strcmp(");
				dumpStructFullPath(fh, "c1", "i1", def, 1, 1);
				fputs(", ", fh);
				dumpStructFullPath(fh, "c2", "i2", def, 1, 1);
				fputs(") != 0) {\n", fh);
					fputts(fh, level, "\tsnprintf(diff, PRINTBUFLEN - 1, \"%s\", \"");
					dumpStructFullPath(fh, "c", "i", def, 1, 0);
					fputs("\");\n\n", fh);
					fputts(fh, level, "\treturn diff;\n");
				fputts(fh, level, "}\n");

				if (!(def->flags & PARAMDEF_RDONLY)) {
					level--;
					fputts(fh, level, "}\n");
				}
				break;
			case	structType:
				makeCmp(fh, def->value.value.structval, level);
				break;
			case	arrayType:
				fputs("\n", fh);
				fputts(fh, level, "i1->idx");
				dumpParamDefCName(fh, def);
				fputs(" = 0;\n", fh);
				fputts(fh, level, "i2->idx");
				dumpParamDefCName(fh, def);
				fputs(" = 0;\n", fh);
				fputts(fh, level, "while (i1->idx");
                dumpParamDefCName(fh, def);
				fputs(" < ", fh);
				dumpStructFullPath(fh, "c1", "i1", def, 1, 1);
				fputs(".n && i2->idx", fh);
                dumpParamDefCName(fh, def);
				fputs(" < ", fh);
				dumpStructFullPath(fh, "c2", "i2", def, 1, 1);
				fputs(".n) {\n", fh);
					makeCmp(fh, def->value.value.arrayval, level + 1);
					fputs("\n", fh);
					fputts(fh, level, "\ti1->idx");
					dumpParamDefCName(fh, def);
					fputs("++;\n", fh);
					fputts(fh, level, "\ti2->idx");
					dumpParamDefCName(fh, def);
					fputs("++;\n", fh);
				fputts(fh, level, "}\n");

				if (!(def->flags & PARAMDEF_RDONLY)) {
					fputts(fh, level, "if (!only_check_rdonly) {\n");
					level++;
				}

				fputts(fh, level, "if (");
				dumpStructFullPath(fh, "c1", "i1", def, 1, 1);
				fputs(".n != ", fh);
				dumpStructFullPath(fh, "c2", "i2", def, 1, 1);
				fputs(".n) {\n", fh);
					fputts(fh, level, "\tsnprintf(diff, PRINTBUFLEN - 1, \"%s\", \"");
					dumpStructFullPath(fh, "c", "i", def, 1, 0);
					fputs("\");\n\n", fh);
					fputts(fh, level, "\treturn diff;\n");
				fputts(fh, level, "}\n");

				if (!(def->flags & PARAMDEF_RDONLY)) {
					level--;
					fputts(fh, level, "}\n");
				}
				break;
			case	builtinType:
				break;
			default:
				fprintf(stderr,"Unknown value.type (%d)\n", def->value.type);
				exit(1);
		}

		if (def->parent && def->parent->value.type == arrayType)
			return;

		def = def->next;
	}
}

void
cDump(FILE *fh, ParamDef *def) {
	const char *name = def->name;
	ParamDef *child_def;

	fputs(
		"/*\n"
		" * Autogenerated file, do not edit it!\n"
		" */\n\n"
		"#include <errno.h>\n"
		"#include <limits.h>\n"
		"#include <inttypes.h>\n"
		"#include <math.h>\n"
		"#include <stdlib.h>\n"
		"#include <string.h>\n"
		"#include <stdio.h>\n\n",
		fh
	);

	for (child_def = def->value.value.structval; child_def != NULL; child_def = child_def->next) {
		if (child_def->value.type == builtinType)
			fprintf(fh, "%s\n", child_def->value.value.stringval);
	}

	fputs(
		"static int\n"
		"cmpNameAtoms(NameAtom *a, NameAtom *b) {\n"
		"\twhile(a && b) {\n"
		"\t\tif (a->name != b->name &&\n"
		"\t\t    (a->name == NULL || b->name == NULL || strcasecmp(a->name, b->name) != 0))\n"
		"\t\t\treturn 0;\n"
		"\t\ta = a->next;\n"
		"\t\tb = b->next;\n"
		"\t}\n"
		"\treturn (a == NULL && b == NULL) ? 1 : 0;\n"
		"}\n"
		"\n",
		fh
	);

	fprintf(fh,
		"void\n"
		"init_%s(%s *c) {\n"
		"\tmemset(c, 0, sizeof(*c));\n"
		"}\n\n",
		name, name);

	makeDefault(fh, def);

	fprintf(fh,
		"void\n"
		"swap_%s(struct %s *c1, struct %s *c2) {\n"
		, name, name, name);

	fprintf(fh,
		"\tstruct %s tmpcfg = *c1;\n"
		"\t*c1 = *c2;\n"
		"\t*c2 = tmpcfg;\n"
		"}\n\n", name);

	fputs("/************** Destroy config  **************/\n\n", fh);
	makeDestroy(fh, def);

	fputs("/************** Parse config  **************/\n\n", fh);

	dumpAcceptDefault(fh, def);

	dumpParamDefCNameRecursive(fh, def->value.value.structval);

	fputs(
		"\n"
		"#define ARRAYALLOC(_x, _n, t_, _chk_ro, _acpt_dflt, _flags)  do {          \\\n"
		"   if ((_n) < 0) return CNF_WRONGINDEX; /* wrong index */                  \\\n"
		"   if ((_n) > ((_x).n)) {                                                  \\\n"
		"      __typeof__(*(_x).val) *v = (_x).val;                                 \\\n"
		"      int j;                                                               \\\n"
		"      if (_chk_ro) return CNF_RDONLY;                                      \\\n"
		"      v = realloc(v, (_n) * sizeof(*v));                                   \\\n"
		"      if (v == NULL) return CNF_NOMEMORY;                                  \\\n"
		"      for (j = (_x).n; j < (_n); j++) {                                    \\\n"
		"          if (_acpt_dflt)                                                  \\\n"
		"              acceptDefault_##t_(&v[j], _flags);                           \\\n"
		"          else                                                             \\\n"
		"              memset(&v[j], 0, sizeof(v[j]));                              \\\n"
		"      }                                                                    \\\n"
		"      (_x).val = v;                                                        \\\n"
		"      (_x).n = (_n);                                                       \\\n"
		"   }                                                                       \\\n"
		"} while(0)\n\n"
		, fh
	);

	fprintf(fh,
		"int\n"
		"confetti_strcmp(char *s1, char *s2);\n\n");

	fprintf(fh,
		"static ConfettyError\n"
		"acceptValue(%s* c, OptDef* opt, int check_rdonly) {\n"
		, name);

	makeAccept(fh, def, 0);

	fputs(
		"\telse {\n"
		"\t\treturn opt->optional ? CNF_OPTIONAL : CNF_MISSED;\n"
		"\t}\n"
		"\treturn CNF_OK;\n"
		"}\n\n",
		fh
	);

	fprintf(fh,
		"static void cleanFlags(%s* c, OptDef* opt);\n\n"
		, name);

	fputs(
		"#define PRINTBUFLEN	8192\n"
		"static char*\n"
		"dumpOptDef(NameAtom *atom) {\n"
		"\tstatic char	buf[PRINTBUFLEN], *ptr;\n"
		"\tint  i = 0;\n\n"
		"\tptr = buf;\n"
		"\twhile(atom) {\n"
		"\t\tif (atom->name) {\n"
		"\t\t\tif (i) ptr += snprintf(ptr, PRINTBUFLEN - 1 - (ptr - buf), \".\");\n"
		"\t\t\tptr += snprintf(ptr, PRINTBUFLEN - 1 - (ptr - buf), \"%s\", atom->name);\n"
		"\t\t}\n"
		"\t\tif (atom->index >= 0)\n"
		"\t\t\tptr += snprintf(ptr, PRINTBUFLEN - 1 - (ptr - buf), \"[%d]\", atom->index);\n"
		"\t\ti = 1;\n"
		"\t\tatom = atom->next;\n"
		"\t}\n"
		"\treturn buf;\n"
		"}\n\n"
		, fh
	);

	fprintf(fh,
		"static void\n"
		"acceptCfgDef(%s *c, OptDef *opt, int check_rdonly, int *n_accepted, int *n_skipped, int *n_optional) {\n"
		"\tConfettyError	r;\n"
		"\tOptDef		*orig_opt = opt;\n\n"
		"\twhile(opt) {\n"
		"\t\tr = acceptValue(c, opt, check_rdonly);\n"
		"\t\tswitch(r) {\n"
		"\t\t\tcase CNF_OK:\n"
		"\t\t\t\tif (n_accepted) (*n_accepted)++;\n"
		"\t\t\t\tbreak;\n"
		"\t\t\tcase CNF_OPTIONAL:\n"
		"\t\t\t\tout_warning(r, \"Option '%cs' is not supported\", dumpOptDef(opt->name));\n"
		"\t\t\t\tif (n_optional) (*n_optional)++;\n"
		"\t\t\t\tbreak;\n"
		"\t\t\tcase CNF_MISSED:\n"
		"\t\t\t\tout_warning(r, \"Could not find '%cs' option\", dumpOptDef(opt->name));\n"
		"\t\t\t\tif (n_skipped) (*n_skipped)++;\n"
		"\t\t\t\tbreak;\n"
		"\t\t\tcase CNF_WRONGTYPE:\n"
		"\t\t\t\tout_warning(r, \"Wrong value type for '%cs' option\", dumpOptDef(opt->name));\n"
		"\t\t\t\tif (n_skipped) (*n_skipped)++;\n"
		"\t\t\t\tbreak;\n"
		"\t\t\tcase CNF_WRONGINDEX:\n"
		"\t\t\t\tout_warning(r, \"Wrong array index in '%cs' option\", dumpOptDef(opt->name));\n"
		"\t\t\t\tif (n_skipped) (*n_skipped)++;\n"
		"\t\t\t\tbreak;\n"
		"\t\t\tcase CNF_RDONLY:\n"
		"\t\t\t\tout_warning(r, \"Could not accept read only '%cs' option\", dumpOptDef(opt->name));\n"
		"\t\t\t\tif (n_skipped) (*n_skipped)++;\n"
		"\t\t\t\tbreak;\n"
		"\t\t\tcase CNF_WRONGINT:\n"
		"\t\t\t\tout_warning(r, \"Could not parse integer value for '%cs' option\", dumpOptDef(opt->name));\n"
		"\t\t\t\tif (n_skipped) (*n_skipped)++;\n"
		"\t\t\t\tbreak;\n"
		"\t\t\tcase CNF_WRONGRANGE:\n"
		"\t\t\t\tout_warning(r, \"Wrong range for '%cs' option\", dumpOptDef(opt->name));\n"
		"\t\t\t\tif (n_skipped) (*n_skipped)++;\n"
		"\t\t\t\tbreak;\n"
		"\t\t\tcase CNF_NOMEMORY:\n"
		"\t\t\t\tout_warning(r, \"Not enough memory to accept '%cs' option\", dumpOptDef(opt->name));\n"
		"\t\t\t\tif (n_skipped) (*n_skipped)++;\n"
		"\t\t\t\tbreak;\n"
		"\t\t\tcase CNF_NOTSET:\n"
		"\t\t\t\tout_warning(r, \"Option '%cs' is not set (or has a default value)\", dumpOptDef(opt->name));\n"
		"\t\t\t\tif (n_skipped) (*n_skipped)++;\n"
		"\t\t\t\tbreak;\n"
		"\t\t\tdefault:\n"
		"\t\t\t\tout_warning(r, \"Unknown error for '%cs' option\", dumpOptDef(opt->name));\n"
		"\t\t\t\tif (n_skipped) (*n_skipped)++;\n"
		"\t\t\t\tbreak;\n"
		"\t\t}\n\n"
		"\t\topt = opt->next;\n"
		"\t}\n"
		"\n"
		"\tcleanFlags(c, orig_opt);\n"
		"}\n\n"
		, name, '%', '%', '%', '%', '%', '%', '%', '%', '%', '%'
	);

	fprintf(fh,
		"int\n"
		"parse_cfg_file_%s(%s *c, FILE *fh, int check_rdonly, int *n_accepted, int *n_skipped, int *n_optional) {\n"
		"\tint error;\n"
		"\tOptDef *option = parseCfgDef(fh, &error);\n"
		"\tif (n_accepted) *n_accepted=0;\n"
		"\tif (n_skipped) *n_skipped=0;\n"
		"\tif (n_optional) *n_optional=0;\n"
		"\tif (option == NULL)\n"
		"\t\treturn error ? -1 : 0;\n"
		"\tacceptCfgDef(c, option, check_rdonly, n_accepted, n_skipped, n_optional);\n"
		"\tfreeCfgDef(option);\n"
		"\treturn 0;\n"
		"}\n\n"
		, name, name
	);

	fprintf(fh,
		"int\n"
		"parse_cfg_buffer_%s(%s *c, char *buffer, int check_rdonly, int *n_accepted, int *n_skipped, int *n_optional) {\n"
		"\tint error;\n"
		"\tOptDef *option = parseCfgDefBuffer(buffer, &error);\n"
		"\tif (n_accepted) *n_accepted=0;\n"
		"\tif (n_skipped) *n_skipped=0;\n"
		"\tif (n_optional) *n_optional=0;\n"
		"\tif (option == NULL)\n"
		"\t\treturn error ? -1 : 0;\n"
		"\tacceptCfgDef(c, option, check_rdonly, n_accepted, n_skipped, n_optional);\n"
		"\tfreeCfgDef(option);\n"
		"\treturn 0;\n"
		"}\n\n"
		, name, name
	);

	fputs(
		"/************** Iterator **************/\n"
		"typedef enum IteratorState {\n"
		"\t_S_Initial = 0,\n"
		, fh );
	makeIteratorStates(fh, def->value.value.structval);
	fputs(
		"\t_S_Finished\n"
		"} IteratorState;\n\n"
		, fh);

	fprintf( fh, "struct %s_iterator_t {\n\tIteratorState\tstate;\n" , name);
	makeArrayIndexes(fh, def->value.value.structval);
	fprintf( fh, "};\n\n");

	fprintf(fh,
		"%s_iterator_t*\n"
		"%s_iterator_init() {\n"
		"\t%s_iterator_t *i = malloc(sizeof(*i));\n"
		"\tif (i == NULL) return NULL;\n"
		"\tmemset(i, 0, sizeof(*i));\n"
		"\treturn i;\n"
		"}\n\n"
		"char*\n"
		"%s_iterator_next(%s_iterator_t* i, %s *c, char **v) {\n"
		"\tstatic char\tbuf[PRINTBUFLEN];\n\n"
		"\t*v = NULL;\n"
		"\tgoto again; /* keep compiler quiet */\n"
		"again:\n"
		"\tswitch(i->state) {\n"
		"\t\tcase _S_Initial:\n"
		, name, name, name, name, name, name);
	makeSwitch(fh, def->value.value.structval, 2, NULL, NULL);
	fprintf(fh,
		"\t\tcase _S_Finished:\n"
		"\t\t\tfree(i);\n"
		"\t\t\tbreak;\n"
		"\t\tdefault:\n"
		"\t\t\tout_warning(CNF_INTERNALERROR, \"Unknown state for %s_iterator_t: %cd\", i->state);\n"
		"\t\t\tfree(i);\n"
		"\t}\n"
		"\treturn NULL;\n"
		"}\n\n"
		, name, '%' );

	fprintf(fh, "/************** Checking of required fields  **************/\nint\ncheck_cfg_%s(%s *c) {\n", name, name);
	fprintf( fh, "\t%s_iterator_t iterator, *i = &iterator;\n" , name);
	fputs("\tint\tres = 0;\n\n", fh);

	makeCheck(fh, def->value.value.structval, 1);

	fputs("\treturn res;\n}\n\n",  fh);

	fprintf(fh,
		"static void\n"
		"cleanFlags(%s* c, OptDef* opt) {\n"
		, name);
	fprintf(fh,
		"\t%s_iterator_t iterator, *i = &iterator;\n\n", name);
	makeCleanFlags(fh, def, 1);
	fputs(
		"\t(void)opt;\n"
		"}\n\n",
		fh
	);

	fputs("/************** Duplicate config  **************/\n\n", fh);
	fprintf(fh,
		"int\n"
		"dup_%s(%s* dst, %s* src) {\n"
		, name, name, name);
	fprintf(fh,
		"\t%s_iterator_t iterator, *i = &iterator;\n\n", name);
	makeDup(fh, def, 1);
	fputs("\n\treturn CNF_OK;\n", fh);
	fputs("}\n\n", fh);

	fputs("/************** Compare config  **************/\n\n", fh);
	fprintf(fh,
		"int\n"
		"confetti_strcmp(char *s1, char *s2) {\n"
			"\tif (s1 == NULL || s2 == NULL) {\n"
				"\t\tif (s1 != s2)\n"
					"\t\t\treturn s1 == NULL ? -1 : 1;\n"
				"\t\telse\n"
					"\t\t\treturn 0;\n"
			"\t}\n\n"
			"\treturn strcmp(s1, s2);\n"
		"}\n\n");

	fprintf(fh,
		"char *\n"
		"cmp_%s(%s* c1, %s* c2, int only_check_rdonly) {\n"
		, name, name, name);
	fprintf(fh,
		"\t%s_iterator_t iterator1, iterator2, *i1 = &iterator1, *i2 = &iterator2;\n"
		"\tstatic char diff[PRINTBUFLEN];\n\n",
		name);
	makeCmp(fh, def, 1);
	fputs("\n\treturn 0;\n", fh);
	fputs("}\n\n", fh);
}
