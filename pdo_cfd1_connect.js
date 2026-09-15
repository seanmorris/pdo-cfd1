/**
 * Registers a PHP connection with a binding from Module.cfd1 and an initial
 * last-insert ID of "0". Included as an EM_JS body by pdo_cfd1_js.h.in.
 *
 * @function cfd1_js_connect
 * @param {number} connection Wasm address of the live pdo_dbh_t, used as a map key.
 * @param {number} name Wasm address of the NUL-terminated UTF-8 binding name.
 * @returns {number} Zero on success, or a Wasm pointer to an allocated UTF-8
 * SQLSTATE:message string. The C caller frees a nonzero pointer with free().
 */
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
