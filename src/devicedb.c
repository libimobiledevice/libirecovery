/*
 * devicedb.c
 * Device database: built-in table merged with optional device files
 *
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * Device files start with an optional "#!format N" line (N = DEVICES_FORMAT_VERSION);
 * a file without it is format 1. Files declaring a newer format are ignored.
 *
 * The table is assembled from, in order (later entries override earlier
 * ones with the same hardware_model):
 *   1. the built-in table generated from data/devices.txt
 *   2. <pkgdatadir>/devices.txt                  (not on Windows)
 *   3. ~/.libirecovery/devices.txt               (%APPDATA%\libirecovery\devices.txt on Windows)
 *      or the file named by $LIBIRECOVERY_DEVICE_DB
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define strcasecmp _stricmp
#define PATH_SEP "\\"
#else
#include <unistd.h>
#include <strings.h>
#include <pwd.h>
#define PATH_SEP "/"
#endif

#include <libimobiledevice-glue/thread.h>

#ifdef IRECV_STATIC
  #define IRECV_API
#elif defined(_WIN32)
  #ifdef DLL_EXPORT
    #define IRECV_API __declspec( dllexport )
  #else
    #define IRECV_API
  #endif
#else
  #if __GNUC__ >= 4
    #define IRECV_API __attribute__((visibility("default")))
  #else
    #define IRECV_API
  #endif
#endif

#include "libirecovery.h"
#include "devicedb.h"

#define DEVICES_FILE_NAME "devices.txt"
#define USER_DIR_NAME     "libirecovery"
#define ENV_DEVICE_DB     "LIBIRECOVERY_DEVICE_DB"
#define DEVICES_FORMAT_VERSION 1
#define MAX_LINE_LENGTH   512
#define MAX_ENTRIES       16384

#define debug(...) if (db_debug) fprintf(stderr, __VA_ARGS__)

struct db_generation {
	struct irecv_device* table;
	void** blocks;
	size_t block_count;
	struct db_generation* prev;
};

struct db_builder {
	struct irecv_device* table;
	size_t count;
	size_t capacity;
	void** blocks;
	size_t block_count;
	size_t block_capacity;
};

static mutex_t db_mutex;
static int db_debug = 0;
static int db_loaded = 0;
static struct db_generation* db_current = NULL;

static char* join_path(const char* dir, const char* name)
{
	size_t len = strlen(dir) + strlen(PATH_SEP) + strlen(name) + 1;
	char* path = malloc(len);
	if (path) {
		snprintf(path, len, "%s%s%s", dir, PATH_SEP, name);
	}
	return path;
}

static char* get_user_dir(void)
{
#ifdef _WIN32
	const char* base = getenv("APPDATA");
	if (!base || !*base) {
		return NULL;
	}
	return join_path(base, USER_DIR_NAME);
#else
	const char* home = getenv("HOME");
	if (!home || !*home) {
		struct passwd* pw = getpwuid(getuid());
		home = pw ? pw->pw_dir : NULL;
	}
	if (!home || !*home) {
		return NULL;
	}
	return join_path(home, "." USER_DIR_NAME);
#endif
}

static char* get_user_file(int* is_default)
{
	const char* env = getenv(ENV_DEVICE_DB);
	char* dir;
	char* path;

	if (env && *env) {
		*is_default = 0;
		return strdup(env);
	}
	*is_default = 1;
	dir = get_user_dir();
	if (!dir) {
		return NULL;
	}
	path = join_path(dir, DEVICES_FILE_NAME);
	free(dir);
	return path;
}

/* A user-controlled file must belong to us (or root) and not be world-writable. */
static int file_is_trusted(FILE* f)
{
#ifdef _WIN32
	(void)f;
	return 1;
#else
	struct stat st;
	if (fstat(fileno(f), &st) != 0 || !S_ISREG(st.st_mode)) {
		return 0;
	}
	if (st.st_uid != geteuid() && st.st_uid != 0) {
		return 0;
	}
	return (st.st_mode & S_IWOTH) == 0;
#endif
}

static int has_control_chars(const char* s)
{
	for (; *s; s++) {
		if ((unsigned char)*s < 0x20 || *s == 0x7f) {
			return 1;
		}
	}
	return 0;
}

static int parse_u32(const char* s, unsigned int* out)
{
	char* end = NULL;
	unsigned long v;

	if (!isdigit((unsigned char)s[0])) {
		return -1;
	}
	errno = 0;
	v = strtoul(s, &end, 0);
	if (errno != 0 || *end != '\0' || v > 0xFFFFFFFFUL) {
		return -1;
	}
	*out = (unsigned int)v;
	return 0;
}

/* Parses one line in place. Returns 1 for an entry, 0 for a blank or comment line, -1 if malformed.
 * The strings in 'dev' point into 'line'. */
static int parse_line(char* line, struct irecv_device* dev)
{
	char* tok[4];
	char* p = line;
	size_t len;
	int i;

	while (isspace((unsigned char)*p)) p++;
	if (*p == '\0' || *p == '#') {
		return 0;
	}
	for (i = 0; i < 4; i++) {
		tok[i] = p;
		while (*p && !isspace((unsigned char)*p)) p++;
		if (*p == '\0') {
			return -1;
		}
		*p++ = '\0';
		while (isspace((unsigned char)*p)) p++;
	}
	len = strlen(p);
	while (len > 0 && isspace((unsigned char)p[len-1])) {
		p[--len] = '\0';
	}
	if (len == 0 || has_control_chars(p)) {
		return -1;
	}
	if (parse_u32(tok[2], &dev->board_id) != 0 || parse_u32(tok[3], &dev->chip_id) != 0) {
		return -1;
	}
	dev->product_type = tok[0];
	dev->hardware_model = tok[1];
	dev->display_name = p;
	return 1;
}

static int builder_add_block(struct db_builder* b, void* block)
{
	if (b->block_count == b->block_capacity) {
		size_t ncap = b->block_capacity ? b->block_capacity * 2 : 16;
		void** nb = realloc(b->blocks, ncap * sizeof(void*));
		if (!nb) {
			return -1;
		}
		b->blocks = nb;
		b->block_capacity = ncap;
	}
	b->blocks[b->block_count++] = block;
	return 0;
}

/* Adds or replaces an entry (matched by hardware_model); copies the strings. */
static int builder_apply(struct db_builder* b, const struct irecv_device* dev)
{
	size_t l1 = strlen(dev->product_type) + 1;
	size_t l2 = strlen(dev->hardware_model) + 1;
	size_t l3 = strlen(dev->display_name) + 1;
	struct irecv_device* slot = NULL;
	char* block;
	size_t i;

	for (i = 0; i < b->count; i++) {
		if (!strcasecmp(b->table[i].hardware_model, dev->hardware_model)) {
			slot = &b->table[i];
			break;
		}
	}
	if (!slot && b->count >= MAX_ENTRIES) {
		return -1;
	}
	if (!slot && b->count + 2 > b->capacity) {
		size_t ncap = b->capacity * 2;
		struct irecv_device* nt = realloc(b->table, ncap * sizeof(struct irecv_device));
		if (!nt) {
			return -1;
		}
		b->table = nt;
		b->capacity = ncap;
	}
	block = malloc(l1 + l2 + l3);
	if (!block) {
		return -1;
	}
	if (builder_add_block(b, block) != 0) {
		free(block);
		return -1;
	}
	memcpy(block, dev->product_type, l1);
	memcpy(block + l1, dev->hardware_model, l2);
	memcpy(block + l1 + l2, dev->display_name, l3);

	if (!slot) {
		slot = &b->table[b->count++];
	}
	slot->product_type = block;
	slot->hardware_model = block + l1;
	slot->display_name = block + l1 + l2;
	slot->board_id = dev->board_id;
	slot->chip_id = dev->chip_id;
	return 0;
}

/* Returns the declared format version (1 if none), or -1 if it is invalid.
 * Only the directive lines before the first entry count. Leaves the file position undefined. */
static int scan_format(FILE* f)
{
	char line[MAX_LINE_LENGTH];
	int version = 1;

	rewind(f);
	while (fgets(line, sizeof(line), f)) {
		char* p = line;
		char* end = NULL;
		long v;

		while (isspace((unsigned char)*p)) p++;
		if (*p == '\0') continue;
		if (*p != '#') break;
		if (strncmp(p, "#!format", 8) != 0 || !isspace((unsigned char)p[8])) continue;
		p += 8;
		while (isspace((unsigned char)*p)) p++;
		errno = 0;
		v = strtol(p, &end, 10);
		if (end == p || errno != 0 || v < 1 || v > 0x7fff) {
			return -1;
		}
		while (isspace((unsigned char)*end)) end++;
		if (*end != '\0') {
			return -1;
		}
		version = (int)v;
		break;
	}
	return version;
}

static int format_supported(FILE* f, const char* path)
{
	int version = scan_format(f);
	if (version < 1 || version > DEVICES_FORMAT_VERSION) {
		debug("%s: %s: unsupported or invalid device file format, ignored\n", __func__, path);
		return 0;
	}
	rewind(f);
	return 1;
}

/* Returns the number of entries applied, or -1 if the file could not be used. */
static int load_file(struct db_builder* b, const char* path, int check_trust)
{
	char line[MAX_LINE_LENGTH];
	int lineno = 0;
	int applied = 0;
	FILE* f = fopen(path, "r");

	if (!f) {
		return -1;
	}
	if (check_trust && !file_is_trusted(f)) {
		debug("%s: ignoring device file with unsafe ownership or permissions: %s\n", __func__, path);
		fclose(f);
		return -1;
	}
	if (!format_supported(f, path)) {
		fclose(f);
		return -1;
	}
	while (fgets(line, sizeof(line), f)) {
		struct irecv_device dev;
		size_t len = strlen(line);
		int r;

		lineno++;
		if (len == sizeof(line) - 1 && line[len-1] != '\n') {
			int c;
			while ((c = fgetc(f)) != EOF && c != '\n') {}
			debug("%s: %s:%d: line too long, skipped\n", __func__, path, lineno);
			continue;
		}
		r = parse_line(line, &dev);
		if (r < 0) {
			debug("%s: %s:%d: malformed entry, skipped\n", __func__, path, lineno);
		} else if (r > 0) {
			if (builder_apply(b, &dev) != 0) {
				debug("%s: %s:%d: unable to add entry\n", __func__, path, lineno);
				break;
			}
			applied++;
		}
	}
	fclose(f);
	return applied;
}

/* Returns NULL if no device file contributed anything, i.e. the built-in table is to be used as is. */
static struct db_generation* build_generation(void)
{
	struct db_builder b;
	struct db_generation* gen = NULL;
	size_t builtin_count = 0;
	int total = 0;
	int is_default = 0;
	char* user_file;
	int r;

	while (irecv_devices_builtin[builtin_count].product_type != NULL) {
		builtin_count++;
	}
	memset(&b, 0, sizeof(b));
	b.capacity = builtin_count + 32;
	b.table = malloc(b.capacity * sizeof(struct irecv_device));
	if (!b.table) {
		return NULL;
	}
	memcpy(b.table, irecv_devices_builtin, builtin_count * sizeof(struct irecv_device));
	b.count = builtin_count;

#if defined(IRECV_DATADIR) && !defined(_WIN32)
	{
		char* sys_file = join_path(IRECV_DATADIR, DEVICES_FILE_NAME);
		if (sys_file) {
			r = load_file(&b, sys_file, 0);
			if (r > 0) total += r;
			free(sys_file);
		}
	}
#endif
	user_file = get_user_file(&is_default);
	if (user_file) {
		r = load_file(&b, user_file, 1);
		if (r > 0) total += r;
		free(user_file);
	}

	if (total > 0) {
		gen = calloc(1, sizeof(*gen));
	}
	if (!gen) {
		size_t i;
		for (i = 0; i < b.block_count; i++) free(b.blocks[i]);
		free(b.blocks);
		free(b.table);
		return NULL;
	}
	memset(&b.table[b.count], 0, sizeof(struct irecv_device));
	b.table[b.count].board_id = (unsigned int)-1;
	b.table[b.count].chip_id = (unsigned int)-1;
	gen->table = b.table;
	gen->blocks = b.blocks;
	gen->block_count = b.block_count;
	return gen;
}

static void free_generation(struct db_generation* gen)
{
	size_t i;
	for (i = 0; i < gen->block_count; i++) free(gen->blocks[i]);
	free(gen->blocks);
	free(gen->table);
	free(gen);
}

void irecv_devicedb_init(void)
{
	mutex_init(&db_mutex);
}

void irecv_devicedb_cleanup(void)
{
	mutex_lock(&db_mutex);
	while (db_current) {
		struct db_generation* prev = db_current->prev;
		free_generation(db_current);
		db_current = prev;
	}
	db_loaded = 0;
	mutex_unlock(&db_mutex);
	mutex_destroy(&db_mutex);
}

void irecv_devicedb_set_debug(int level)
{
	db_debug = level;
}

struct irecv_device* irecv_devicedb_get(void)
{
	struct irecv_device* table;

	mutex_lock(&db_mutex);
	if (!db_loaded) {
		db_current = build_generation();
		db_loaded = 1;
	}
	table = db_current ? db_current->table : irecv_devices_builtin;
	mutex_unlock(&db_mutex);
	return table;
}

/* Re-reads all device files. Tables handed out earlier remain valid. */
static void reload(void)
{
	struct db_generation* gen;

	mutex_lock(&db_mutex);
	gen = build_generation();
	if (gen) {
		gen->prev = db_current;
		db_current = gen;
	}
	db_loaded = 1;
	mutex_unlock(&db_mutex);
}

static int token_valid(const char* s)
{
	if (!s || !*s) {
		return 0;
	}
	for (; *s; s++) {
		if (isspace((unsigned char)*s) || (unsigned char)*s < 0x20 || *s == 0x7f) {
			return 0;
		}
	}
	return 1;
}

static int name_valid(const char* s)
{
	size_t len;
	if (!s || !*s || has_control_chars(s)) {
		return 0;
	}
	len = strlen(s);
	return !isspace((unsigned char)s[0]) && !isspace((unsigned char)s[len-1]);
}

static irecv_error_t add_entry(const char* product_type, const char* hardware_model, unsigned int board_id, unsigned int chip_id, const char* display_name)
{
	struct irecv_device* table;
	char* path;
	int is_default = 0;
	FILE* f;
	long size;
	int ok;
	size_t i;

	/* the whole line, including separators and the IDs, must fit what the loader accepts */
	if (!name_valid(display_name) ||
	    strlen(product_type) + strlen(hardware_model) + strlen(display_name) + 32 > MAX_LINE_LENGTH) {
		return IRECV_E_INVALID_INPUT;
	}

	table = irecv_devicedb_get();
	for (i = 0; table[i].product_type != NULL; i++) {
		if (!strcasecmp(table[i].hardware_model, hardware_model)) {
			if (!strcmp(table[i].product_type, product_type) &&
			    table[i].board_id == board_id && table[i].chip_id == chip_id &&
			    !strcmp(table[i].display_name, display_name)) {
				return IRECV_E_SUCCESS;
			}
			break;
		}
	}

	path = get_user_file(&is_default);
	if (!path) {
		return IRECV_E_UNKNOWN_ERROR;
	}
	if (is_default) {
		char* dir = get_user_dir();
		if (dir) {
#ifdef _WIN32
			_mkdir(dir);
#else
			mkdir(dir, 0700);
#endif
			free(dir);
		}
	}

	f = fopen(path, "a+");
	free(path);
	if (!f) {
		return IRECV_E_UNKNOWN_ERROR;
	}
	if (!file_is_trusted(f)) {
		fclose(f);
		return IRECV_E_UNKNOWN_ERROR;
	}

	if (!format_supported(f, "user device file")) {
		fclose(f);
		return IRECV_E_UNSUPPORTED;
	}

	/* a missing final newline must not glue our line onto the previous entry */
	ok = (fseek(f, 0, SEEK_END) == 0);
	size = ok ? ftell(f) : -1;
	if (size == 0) {
		ok = fputs("#!format 1\n"
		           "# libirecovery user device database\n"
		           "# <product_type> <hardware_model> <board_id> <chip_id> <display name>\n"
		           "# Entries override built-in ones with the same hardware_model.\n", f) >= 0;
	} else if (size > 0) {
		if (fseek(f, -1, SEEK_END) != 0) {
			ok = 0;
		} else if (fgetc(f) != '\n') {
			ok = (fseek(f, 0, SEEK_END) == 0) && fputc('\n', f) != EOF;
		}
	} else {
		ok = 0;
	}
	if (ok) {
		ok = fprintf(f, "%s %s 0x%02x 0x%04x %s\n", product_type, hardware_model, board_id, chip_id, display_name) > 0;
	}
	if (fclose(f) != 0) {
		ok = 0;
	}
	if (!ok) {
		return IRECV_E_UNKNOWN_ERROR;
	}

	reload();
	return IRECV_E_SUCCESS;
}

irecv_error_t irecv_devices_add(const char* product_type, const char* hardware_model, unsigned int board_id, unsigned int chip_id, const char* display_name)
{
	struct irecv_device* table;
	const struct irecv_device* existing = NULL;
	const char* name = display_name;
	char* generated = NULL;
	irecv_error_t err;
	size_t i;

	if (!token_valid(product_type) || !token_valid(hardware_model)) {
		return IRECV_E_INVALID_INPUT;
	}

	table = irecv_devicedb_get();
	for (i = 0; table[i].product_type != NULL; i++) {
		if (!strcasecmp(table[i].hardware_model, hardware_model)) {
			existing = &table[i];
		} else if (table[i].chip_id == chip_id && table[i].board_id == board_id) {
			/* the existing entry would shadow the new one in lookups by chip/board ID */
			return IRECV_E_INVALID_INPUT;
		}
	}

	if (!name || !*name) {
		if (existing) {
			name = existing->display_name;
		} else {
			size_t len = strlen(product_type) + strlen(hardware_model) + 4;
			generated = malloc(len);
			if (!generated) {
				return IRECV_E_OUT_OF_MEMORY;
			}
			snprintf(generated, len, "%s (%s)", product_type, hardware_model);
			name = generated;
		}
	}

	err = add_entry(product_type, hardware_model, board_id, chip_id, name);
	free(generated);
	return err;
}
