const state = Module['__pdoCfd1'];
try
{
	const key = UTF8ToString(name), bindings = Module['cfd1'];
	if(!bindings
		|| !Object.prototype.hasOwnProperty.call(bindings, key)
		|| !bindings[key]
		|| typeof bindings[key].prepare !== 'function'
	){
		throw new Error('Missing or invalid D1 binding: cfd1.' + key);
	}
	state.connections.set(connection, { binding: bindings[key], lastId: '0' });
	return 0;
}
catch(error)
{
	return state.error(error);
}
