/**
 * Binds every statement before issuing one atomic D1 batch, then validates all
 * results before publishing them. Failure clears all affected result buffers;
 * every attempt clears collected parameters. The batch is not retried.
 *
 * C validates connection ownership and distinct statements, retains their
 * lifetimes, and collects parameters before calling this EM_ASYNC_JS body.
 * The C caller receives the resolved result after Asyncify resumes execution.
 *
 * @function cfd1_js_batch
 * @async
 * @param {number} connection Wasm address of the registered owning pdo_dbh_t.
 * @param {number} statements Wasm byte address of an array of 32-bit pdo_stmt_t
 * pointers, read through HEAPU32 before the first await.
 * @param {number} count Number of statement pointers in the array.
 * @returns {Promise<number>} Zero on success, or a Wasm pointer to an allocated
 * UTF-8 SQLSTATE:message string. C frees a nonzero pointer with free().
 */
const state = Module['__pdoCfd1'];
const ids = Array.from(HEAPU32.subarray(statements >>> 2, (statements >>> 2) + count));
const data = ids.map(id => state.statements.get(id));
try
{
	const binding = state.connections.get(connection).binding;
	if(typeof binding.batch !== 'function') state.fail('HYC00', 'This D1 binding does not support atomic batches');
	const prepared = data.map(statement => state.bind(statement));
	const results = await binding.batch(prepared);
	if(!Array.isArray(results) || results.length !== count) throw new Error('Invalid D1 batch result count');
	// Validate every result before exposing rows or changing any insert IDs.
	const normalized = results.map(result => state.result(result));
	normalized.forEach((result, i) => state.publish(data[i], result));
	return 0;
}
catch(error)
{
	data.forEach(statement => state.reset(statement));
	return state.error(error);
}
finally
{
	data.forEach(statement => { statement.params = []; });
}
