/* pdo_cfd1 extension for PHP */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "ext/standard/php_var.h"
#include "../pdo/php_pdo_driver.h"
#include "php_pdo_cfd1.h"
#include "zend_API.h"
#include "zend_types.h"
#include "zend_closures.h"
#include <emscripten.h>
#include "zend_hash.h"

#if PHP_MAJOR_VERSION >= 8
# include "zend_attributes.h"
#else
# include <stdbool.h>
#endif

/* For compatibility with older PHP versions */
#ifndef ZEND_PARSE_PARAMETERS_NONE
#define ZEND_PARSE_PARAMETERS_NONE() \
	ZEND_PARSE_PARAMETERS_START(0, 0) \
	ZEND_PARSE_PARAMETERS_END()
#endif

int pdo_cfd1_error(
	pdo_dbh_t *dbh,
	pdo_stmt_t *stmt,
	int errcode,
	const char *sqlstate,
	const char *errmsg,
	const char *file,
	int line
){
	pdo_cfd1_db_handle *handle = (pdo_cfd1_db_handle*) dbh->driver_data;
	pdo_error_type *pdo_err = stmt ? &stmt->error_code : &dbh->error_code;
	// pdo_cfd1_error_info *einfo = &handle->einfo;

	handle->einfo.errcode = errcode;
	handle->einfo.file = file;
	handle->einfo.line = line;

	if(errmsg)
	{
		handle->einfo.errmsg = pestrdup(errmsg, dbh->is_persistent);
	}

	if(sqlstate)
	{
		handle->einfo.sqlstate = pestrdup(sqlstate, dbh->is_persistent);
	}

	if(sqlstate == NULL || strlen(sqlstate) >= sizeof(pdo_error_type))
	{
		strcpy(*pdo_err, "HY000");
	}
	else
	{
		strcpy(*pdo_err, sqlstate);
	}

	if(!dbh->methods)
	{
		pdo_throw_exception(handle->einfo.errcode, handle->einfo.errmsg, pdo_err);
	}

	return errcode;
}

#include "pdo_cfd1_db_statement.c"
#include "pdo_cfd1_db.c"

PHP_RINIT_FUNCTION(pdo_cfd1)
{
	return SUCCESS;
}

PHP_MINIT_FUNCTION(pdo_cfd1)
{
	// REGISTER_INI_ENTRIES();
#if defined(ZTS) && defined(COMPILE_DL_PDO_CFD1)
	ZEND_TSRMLS_CACHE_UPDATE();
#endif
	return php_pdo_register_driver(&pdo_cfd1_driver);
}

PHP_MINFO_FUNCTION(pdo_cfd1)
{
	php_info_print_table_start();
	php_info_print_table_row(2, "CloudFlare D1 SQL support for PDO", "enabled");
	php_info_print_table_row(2, "CloudFlare D1 SQL module detected",
		EM_ASM_INT({ return typeof Module.cfd1 === 'object' && Object.keys(Module.cfd1).length }) ? "yes" : "no"
	);
	php_info_print_table_end();

	// DISPLAY_INI_ENTRIES();
}

PHP_MSHUTDOWN_FUNCTION(pdo_cfd1)
{
	// UNREGISTER_INI_ENTRIES();
	return SUCCESS;
}

zend_module_entry pdo_cfd1_module_entry = {
	STANDARD_MODULE_HEADER,
	"pdo_cfd1",
	NULL,                    /* zend_function_entry */
	PHP_MINIT(pdo_cfd1),     /* PHP_MINIT - Module initialization */
	PHP_MSHUTDOWN(pdo_cfd1), /* PHP_MSHUTDOWN - Module shutdown */
	PHP_RINIT(pdo_cfd1),     /* PHP_RINIT - Request initialization */
	NULL,                    /* PHP_RSHUTDOWN - Request shutdown */
	PHP_MINFO(pdo_cfd1),     /* PHP_MINFO - Module info */
	PHP_PDO_CFD1_VERSION,    /* Version */
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_PDO_CFD1
# ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
# endif
ZEND_GET_MODULE(pdo_cfd1)
#endif
