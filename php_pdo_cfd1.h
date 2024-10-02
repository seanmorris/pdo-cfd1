/* pdo_CFD1 extension for PHP */

#ifndef PHP_PDO_CFD1_H
# define PHP_PDO_CFD1_H

extern zend_module_entry pdo_cfd1_module_entry;
# define phpext_pdo_cfd1_ptr &pdo_cfd1_module_entry

# define PHP_PDO_CFD1_VERSION "0.0.0"

# if defined(ZTS) && defined(COMPILE_DL_PDO_CFD1)
ZEND_TSRMLS_CACHE_EXTERN()
# endif

#include <stdbool.h>
#include "../vrzno/php_vrzno.h"

typedef struct {
	unsigned int errcode;
	int sqlstate;
	char *errmsg;
	const char *file;
	int line;
} pdo_cfd1_db_error_info;

typedef struct {
	jstarget *targetId;
	pdo_cfd1_db_error_info einfo;
} pdo_cfd1_db_handle;

typedef struct {
	pdo_cfd1_db_handle *db;
	vrzno_object *stmt;
	unsigned long curr;
	unsigned long row_count;
	unsigned pre_fetched:1;
	unsigned done:1;
	zval results;
} pdo_cfd1_stmt;

#endif
