#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <prscfg.h>
#include <my_product_cfg.h>

static int useStdout = 0;
void
out_warning(ConfettyError r __attribute__ ((unused)), char *format, ...) {
    va_list args;

	va_start(args, format);
	vfprintf(useStdout ? stdout : stderr, format, args);
	va_end(args);
	if (useStdout)
		fputc('\n', stdout);
}

int
main(int argc, char* argv[]) {
	my_product	cfg, dup_cfg;
	my_product_iterator_t	*i;
	char		*key, *value;

	init_my_product(&cfg);
	fill_default_my_product(&cfg, CNF_FLAG_STRUCT_NOTSET);

	if (argc > 1) {
		cfg.array_of_ro_structs.val = malloc(1 * sizeof(cfg.array_of_ro_structs));
		cfg.array_of_ro_structs.n = 1;
		cfg.array_of_ro_structs.__confetti_flags = 0;
		cfg.array_of_ro_structs.val[0].__confetti_flags = 0;
		cfg.array_of_ro_structs.val[0].f1 = 1;
		cfg.array_of_ro_structs.val[0].f2 = 2;
	}

	if (argc > 1) {
		int			nAccepted, nSkipped, nOptional;
		FILE *fh = fopen(argv[1], "r");

		if (!fh) {
			fprintf(stderr, "Could not open file %s\n", argv[1]);
			return 1;
		}

		useStdout = 1;
		parse_cfg_file_my_product(&cfg, fh, 1, &nAccepted, &nSkipped, &nOptional);

		printf("==========Accepted: %d; Skipped: %d; Optional: %d===========\n", nAccepted, nSkipped, nOptional);

		fclose(fh);
	}

	i = my_product_iterator_init();
	while ( (key = my_product_iterator_next(i, &cfg, &value)) != NULL ) {
		if (value) {
			printf("%s => '%s'\n", key, value);
			free(value);
		} else {
			printf("%s => (null)\n", key);
		}
	}

	printf("==========Check===========\n");
	useStdout = 1;
	printf("Missed required: %d\n", check_cfg_my_product(&cfg));

	unsigned k;
	for (k = 0; k < cfg.root_array.n; k++) {
		if (!CNF_STRUCT_DEFINED(&cfg.root_array.val[k]))
			printf("root_array[%u] is not defined\n", k);
	}

	printf("==========Dup=============\n");
	init_my_product(&dup_cfg);
	dup_my_product(&dup_cfg, &cfg);
	i = my_product_iterator_init();
	while ( (key = my_product_iterator_next(i, &dup_cfg, &value)) != NULL ) {
		if (value) {
			printf("%s => '%s'\n", key, value);
			free(value);
		} else {
			printf("%s => (null)\n", key);
		}
	}

	printf("==========Compare=========\n");
	if (cmp_my_product(&cfg, &dup_cfg, 0) != 0)
		printf("total: differ\n");
	else
		printf("total: equal\n");
	if (cmp_my_product(&cfg, &dup_cfg, 1) != 0)
		printf("only_rdonly: differ\n");
	else
		printf("only_rdonly: equal\n");

	printf("==========Destroy=========\n");
	destroy_my_product(&cfg);
	destroy_my_product(&dup_cfg);

	return 0;
}
