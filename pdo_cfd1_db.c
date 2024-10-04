static void cfd1_handle_closer(pdo_dbh_t *dbh)
{
	// EM_ASM({ console.log('CLOSE', $0);}, dbh);
}

static bool cfd1_handle_preparer(pdo_dbh_t *dbh, zend_string *sql, pdo_stmt_t *stmt, zval *driver_options)
{
	pdo_cfd1_db_handle *handle = dbh->driver_data;
	pdo_cfd1_stmt *vStmt = emalloc(sizeof(pdo_cfd1_stmt));

	stmt->methods = &vrzno_stmt_methods;
	stmt->driver_data = vStmt;

	const char *sqlString = ZSTR_VAL(sql);

	EM_ASM({
		const dbName = UTF8ToString($0);
		const query = UTF8ToString($1);
		const zv = $2;
		
		const prepared = Module.cfd1[dbName].prepare(query);
		Module.jsToZval(prepared, zv);

	}, dbh->data_source, sqlString, &vStmt->prepared);

	vStmt->db = handle;
	
	return true;
}

static zend_long cfd1_handle_doer(pdo_dbh_t *dbh, const zend_string *sql)
{
	return 1;
}

static zend_string *cfd1_handle_quoter(pdo_dbh_t *dbh, const zend_string *unquoted, enum pdo_param_type paramtype)
{
	const char *unquotedChar = ZSTR_VAL(unquoted);
	zend_string *quoted = zend_string_init(unquotedChar, strlen(unquotedChar), 0);
	return quoted;
}

static bool cfd1_handle_begin(pdo_dbh_t *dbh)
{
	return (bool) EM_ASM_INT({
		console.log('BEGIN TXN');
		return true;
	});
}

static bool cfd1_handle_commit(pdo_dbh_t *dbh)
{
	return (bool) EM_ASM_INT({
		console.log('COMMIT TXN', $0);
		return true;
	});
}

static bool cfd1_handle_rollback(pdo_dbh_t *dbh)
{
	return (bool) EM_ASM_INT({
		console.log('ROLLBACK TXN', $0);
		return true;
	});
}

static bool pdo_cfd1_set_attr(pdo_dbh_t *dbh, zend_long attr, zval *val)
{
	return (bool) EM_ASM_INT({
		console.log('SET ATTR', $1, $2);
		return true;
	}, attr, val);
}

static zend_string *pdo_cfd1_last_insert_id(pdo_dbh_t *dbh, const zend_string *name)
{
	const char *nameStr = ZSTR_VAL(name);
#if PHP_MAJOR_VERSION >= 8 && PHP_MINOR_VERSION >= 1
	return zend_ulong_to_str
#else
	return zend_long_to_str
#endif
	(EM_ASM_INT({
		console.log('LAST INSERT ID', UTF8ToString($0));
		return 0;
	}, &nameStr));
}

static void pdo_cfd1_fetch_error_func(pdo_dbh_t *dbh, pdo_stmt_t *stmt, zval *info)
{
	EM_ASM({
		console.log('FETCH ERROR FUNC', $0, $1);
	}, stmt, info);
}

static int pdo_cfd1_get_attr(pdo_dbh_t *dbh, zend_long attr, zval *return_value)
{
	return EM_ASM_INT({
		console.log('GET ATTR', $0, $1);
		return 0;
	}, attr, return_value);
}

static void pdo_cfd1_request_shutdown(pdo_dbh_t *dbh)
{
	EM_ASM({ console.log('SHUTDOWN'); });
}

static void pdo_cfd1_get_gc(pdo_dbh_t *dbh, zend_get_gc_buffer *gc_buffer)
{
	EM_ASM({ console.log('GET GC', $0);}, gc_buffer);
}

static const struct pdo_dbh_methods cfd1_db_methods = {
	cfd1_handle_closer,
	cfd1_handle_preparer,
	cfd1_handle_doer,
	cfd1_handle_quoter,
	cfd1_handle_begin,
	cfd1_handle_commit,
	cfd1_handle_rollback,
	pdo_cfd1_set_attr,
	pdo_cfd1_last_insert_id,
	pdo_cfd1_fetch_error_func,
	pdo_cfd1_get_attr,
	NULL,	/* check_liveness: not needed */
	NULL, //get_driver_methods,
	pdo_cfd1_request_shutdown,
	NULL, /* in transaction, use PDO's internal tracking mechanism */
	pdo_cfd1_get_gc
};

static int pdo_cfd1_db_handle_factory(pdo_dbh_t *dbh, zval *driver_options)
{
	pdo_cfd1_db_handle *handle;
	handle = pecalloc(1, sizeof(pdo_cfd1_db_handle), dbh->is_persistent);

	handle->einfo.errcode = 0;
	handle->einfo.errmsg = NULL;
	dbh->driver_data = handle;
	dbh->methods = &cfd1_db_methods;

	EM_ASM({
		if(typeof Module.cfd1 !== 'object')
		{
			throw new Error("The `cfd1` object must be provided as a constructor arg to PHP to use pdo_cfd1.");
		}

		const dbName = UTF8ToString($0);
		
		if(typeof Module.cfd1[dbName] !== 'object')
		{
			throw new Error(`The value provided at cfd1[${dbName}] does not exist or is not an object.`);
		}
	}, dbh->data_source);

	return 1;
}

const pdo_driver_t pdo_cfd1_driver = {
	PDO_DRIVER_HEADER(cfd1),
	pdo_cfd1_db_handle_factory
};
