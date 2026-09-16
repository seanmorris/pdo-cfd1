/**
 * Resolves a PDO parameter to its zero-based D1 slot using the prepared query's
 * binding mode. Included as an EM_JS body by pdo_cfd1_js.h.in.
 *
 * @function cfd1_js_parameter_index
 * @param {number} statement Wasm address of an already registered pdo_stmt_t.
 * @param {number} name Wasm address of a NUL-terminated UTF-8 name including its
 * leading colon, or zero to select positional lookup.
 * @param {number} position Zero-based positional slot; ignored when name is nonzero.
 * @returns {number} The zero-based slot, or -1 for an unknown name, invalid
 * position, or incompatible binding mode.
 */
const data = Module['__pdoCfd1'].statements.get(statement);
if(name) return data.names.has(UTF8ToString(name)) ? data.names.get(UTF8ToString(name)) : -1;
return data.mode !== 'named' && position >= 0 && position < data.count ? position : -1;
