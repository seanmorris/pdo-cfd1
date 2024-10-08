static int pdo_cfd1_stmt_dtor(pdo_stmt_t *stmt)
{
	pdo_cfd1_stmt *vStmt = (pdo_cfd1_stmt*)stmt->driver_data;
	efree(vStmt);
	return 1;
}

EM_ASYNC_JS(int, pdo_cfd1_real_stmt_execute, (zval *zv, zval *rv), {
	const statement = Module.zvalToJS(zv);

	if(!Module.PdoParams.has(statement))
	{
		Module.PdoParams.set(statement, []);
	}

	const paramList = Module.PdoParams.get(statement);

	const bound = paramList.length
		? statement.bind(...paramList)
		: statement;

	let result = false;
	try
	{
		result = await bound.run();
	}
	catch(error)
	{
		console.error(error);
		return false;
	}

	Module.PdoParams.delete(statement);

	if(!result.success)
	{
		return false;
	}

	Module.jsToZval(result.results, rv);

	return true;
});

static int pdo_cfd1_stmt_execute(pdo_stmt_t *stmt)
{
	pdo_cfd1_stmt *vStmt = (pdo_cfd1_stmt*)stmt->driver_data;

	stmt->column_count = 0;
	vStmt->row_count = 0;
	vStmt->curr = 0;
	vStmt->done = 0;

	pdo_cfd1_real_stmt_execute(&vStmt->prepared, &vStmt->results);

	vStmt->row_count = EM_ASM_INT({
		const results = Module.zvalToJS($0);
		if(results) return results.length;
		return 0;
	}, &vStmt->results);

	if(vStmt->row_count)
	{
		stmt->column_count = EM_ASM_INT({
			const results = Module.zvalToJS($0);
			if(results.length) return Object.keys(results[0]).length;
			return 0;
		}, &vStmt->results);
	}

	stmt->executed = 1;
	return true;
}

static int pdo_cfd1_stmt_fetch(pdo_stmt_t *stmt, enum pdo_fetch_orientation ori, zend_long offset)
{
	pdo_cfd1_stmt *vStmt = (pdo_cfd1_stmt*)stmt->driver_data;

	if(stmt->executed != 1)
	{
		return 0;
	}

	int advanced = EM_ASM_INT({
		const targetId = $0;
		const target = Module.targets.get(targetId);
		const current = $1;

		if(current >= target.length)
		{
			return false;
		}

		return true;

	}, vrzno_fetch_object(Z_OBJ(vStmt->results))->targetId, vStmt->curr);

	if(advanced)
	{
		vStmt->curr++;
	}
	else
	{
		vStmt->done = 1;
	}

	return advanced;
}

static int pdo_cfd1_stmt_describe_col(pdo_stmt_t *stmt, int colno)
{
	pdo_cfd1_stmt *vStmt = (pdo_cfd1_stmt*)stmt->driver_data;

	if(colno >= stmt->column_count) {
		return 0;
	}

	char *colName = EM_ASM_PTR({

		const results = Module.targets.get($0);

		if(results.length)
		{
			const jsRet = Object.keys(results[0])[$1];

			const len    = lengthBytesUTF8(jsRet) + 1;
			const strLoc = _malloc(len);

			stringToUTF8(jsRet, strLoc, len);

			return strLoc;
		}

		return 0;

	}, vrzno_fetch_object(Z_OBJ(vStmt->results))->targetId, colno);

	if(!colName)
	{
		return 0;
	}

	stmt->columns[colno].name = zend_string_init(colName, strlen(colName), 0);
	stmt->columns[colno].maxlen = SIZE_MAX;
	stmt->columns[colno].precision = 0;

	free(colName);

	return 1;
}

static int pdo_cfd1_stmt_get_col(pdo_stmt_t *stmt, int colno, zval *zv, enum pdo_param_type *type)
{
	pdo_cfd1_stmt *vStmt = (pdo_cfd1_stmt*)stmt->driver_data;

	if(colno >= stmt->column_count)
	{
		return 0;
	}

	EM_ASM_PTR({
		const results = Module.targets.get($0);
		const current = -1 + $1;
		const colno   = $2;
		const rv = $3;

		if(current >= results.length)
		{
			return null;
		}

		const result = results[current];
		const key = Object.keys(result)[$2];

		Module.jsToZval(result[key], rv);

	}, vrzno_fetch_object(Z_OBJ(vStmt->results))->targetId, vStmt->curr, colno, zv);

	return 1;
}

static int pdo_cfd1_stmt_param_hook(pdo_stmt_t *stmt, struct pdo_bound_param_data *param, enum pdo_param_event event_type)
{
	if(event_type != PDO_PARAM_EVT_ALLOC)
	{
		return true;
	}

	pdo_cfd1_stmt *vStmt = (pdo_cfd1_stmt*)stmt->driver_data;

	EM_ASM({
		const statement = Module.zvalToJS($0);
		const paramVal  = Module.zvalToJS($1);

		if(!Module.PdoParams.has(statement))
		{
			Module.PdoParams.set(statement, []);
		}

		const paramList = Module.PdoParams.get(statement);

		paramList.push(paramVal);

	}, &vStmt->prepared, param->parameter);

	return true;
}

static int pdo_cfd1_stmt_get_attribute(pdo_stmt_t *stmt, zend_long attr, zval *val)
{
	// EM_ASM({ console.log('GET ATTR', $0, $1, $2); }, stmt, attr, val);
	return 1;
}

static int pdo_cfd1_stmt_col_meta(pdo_stmt_t *stmt, zend_long colno, zval *return_value)
{
	// EM_ASM({ console.log('COL META', $0, $1, $2); }, stmt, colno, return_value);
	return 1;
}

static int pdo_cfd1_stmt_cursor_closer(pdo_stmt_t *stmt)
{
	EM_ASM({ console.log('CLOSE', $0, $1, $2); }, stmt);
	return 1;
}

const struct pdo_stmt_methods vrzno_stmt_methods = {
	pdo_cfd1_stmt_dtor,
	pdo_cfd1_stmt_execute,
	pdo_cfd1_stmt_fetch,
	pdo_cfd1_stmt_describe_col,
	pdo_cfd1_stmt_get_col,
	pdo_cfd1_stmt_param_hook,
	NULL, /* set_attr */
	pdo_cfd1_stmt_get_attribute, /* get_attr */
	pdo_cfd1_stmt_col_meta,
	NULL, /* next_rowset */
	pdo_cfd1_stmt_cursor_closer
};
