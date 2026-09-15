/**
 * Binds the parameters collected by C, awaits one D1 run(), and publishes its
 * validated results. Failure clears buffered results; every attempt clears
 * collected parameters. The query is not retried.
 *
 * This async body is embedded by EM_ASYNC_JS in pdo_cfd1_js.h.in. The C caller
 * receives the resolved numeric result after Asyncify resumes execution.
 *
 * @function cfd1_js_execute
 * @async
 * @param {number} statement Wasm address of a registered pdo_stmt_t with its
 * parameters already collected in the shared statement state.
 * @returns {Promise<number>} Zero on success, or a Wasm pointer to an allocated
 * UTF-8 SQLSTATE:message string. C frees a nonzero pointer with free().
 */
const state = Module['__pdoCfd1'], data = state.statements.get(statement);
try
{
	state.publish(data, state.result(await state.bind(data).run()));
	return 0;
}
catch(error)
{
	state.reset(data);
	return state.error(error);
}
finally
{
	data.params = [];
}
