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
