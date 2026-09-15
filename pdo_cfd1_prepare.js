/**
 * Parses placeholders, prepares a D1 statement, and registers its execution
 * state without running a query. Included as an EM_JS body by pdo_cfd1_js.h.in.
 *
 * @function cfd1_js_prepare
 * @param {number} statement Wasm address of the live pdo_stmt_t, used as a map key.
 * @param {number} connection Wasm address of an already registered pdo_dbh_t.
 * @param {number} sql Wasm address of the UTF-8 SQL text.
 * @param {number} length SQL length in bytes, excluding the NUL terminator.
 * @returns {number} Zero on success, or a Wasm pointer to an allocated UTF-8
 * SQLSTATE:message string. The C caller frees a nonzero pointer with free().
 */
const state = Module['__pdoCfd1'];
try
{
	const parsed = state.parse(UTF8ToString(sql, length));
	const data = { ...parsed, connection, prepared: state.connections.get(connection).binding.prepare(parsed.sql) };
	state.reset(data);
	state.statements.set(statement, data);
	return 0;
}
catch(error)
{
	return state.error(error);
}
