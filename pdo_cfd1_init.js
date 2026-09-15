/**
 * @typedef {Object<string, null|number|string|number[]>} Cfd1Row
 * A result row with finite numeric values and BLOBs represented as byte arrays.
 */

/**
 * @typedef {object} Cfd1Query
 * @property {string} sql SQL with supported placeholders rewritten to ?N slots.
 * @property {number} count Highest one-based slot index, or zero with no parameters.
 * @property {""|"named"|"positional"} mode Empty when there are no parameters.
 * @property {Map<string, number>} names Colon-prefixed names mapped to zero-based slots.
 * @property {Set<number>} required Zero-based slots actually referenced by the SQL.
 * @property {boolean} inserts Whether the scan recognizes an INSERT or REPLACE.
 */

/**
 * @typedef {object} Cfd1Connection
 * @property {object} binding The D1 binding selected from Module.cfd1.
 * @property {string|null} lastId Decimal insert ID, initially "0"; null when an
 * insert changes rows but D1 omits a safe integer ID.
 */

/**
 * @typedef {object} Cfd1Statement
 * @property {string} sql Rewritten SQL from the query scan.
 * @property {number} count Number of positional slots, including unused gaps.
 * @property {""|"named"|"positional"} mode Query binding mode.
 * @property {Map<string, number>} names Colon-prefixed names mapped to zero-based slots.
 * @property {Set<number>} required Slots that must have collected parameter values.
 * @property {boolean} inserts Whether this query can update the connection's insert ID.
 * @property {number} connection Wasm address identifying the owning PDO connection.
 * @property {object} prepared D1 prepared statement used for bind/run/batch operations.
 * @property {Array<null|number|string|Uint8Array>} params Sparse, zero-based values
 * collected by C for the next execution; cleared after each attempt.
 * @property {Cfd1Row[]} rows Buffered result rows; empty after reset or failure.
 * @property {string[]} columns Column names taken from the first result row.
 * @property {number} changes Affected-row count, or zero after reset.
 */

/**
 * @typedef {object} Cfd1Result
 * @property {Cfd1Row[]} rows Validated result rows.
 * @property {string[]} columns Keys from the first row, or an empty array.
 * @property {number} changes Nonnegative safe integer affected-row count.
 * @property {object} meta D1 metadata, or an empty object when absent.
 */

/**
 * Installs connection/statement registries and shared D1 helpers on this
 * Emscripten Module. PHP calls this EM_JS body during extension startup,
 * before registering connections or preparing statements.
 *
 * @function cfd1_js_init
 * @returns {void}
 */
Module['__pdoCfd1'] = {
	statements: /** @type {Map<number, Cfd1Statement>} */ (new Map())
	, connections: /** @type {Map<number, Cfd1Connection>} */ (new Map())
	, /**
	 * Throws a validation error carrying a PDO SQLSTATE for the error bridge.
	 * @param {string} sqlstate Five-character SQLSTATE.
	 * @param {string} message Human-readable error detail.
	 * @returns {never}
	 * @throws {Error} An error with its sqlstate property set.
	 */
	fail(sqlstate, message) {
		const error = new Error(message);
		error.sqlstate = sqlstate;
		throw error;
	}
	, /**
	 * Serializes a caught value as a NUL-terminated UTF-8 SQLSTATE:message in
	 * Wasm memory, defaulting to HY000 when the value has no SQLSTATE.
	 * @param {*} error Value caught from a validation or D1 operation.
	 * @returns {number} Allocated Wasm byte address; the C error bridge owns the
	 * buffer and releases it with free().
	 */
	error(error) {
		const state = error && error.sqlstate || 'HY000';
		const message = state + ':' + (error && error.message || String(error));
		const size = lengthBytesUTF8(message) + 1, pointer = _malloc(size);
		stringToUTF8(message, pointer, size);
		return pointer;
	}
	, /**
	 * Rewrites named, bare, and numbered placeholders while preserving SQL
	 * literals, quoted identifiers, and comments. Slot indexes are limited to
	 * 1-100. D1 performs SQL syntax validation when the statement is prepared/run.
	 * @param {string} query Decoded SQL text.
	 * @returns {Cfd1Query} Rewritten SQL, binding slots, and insert recognition.
	 * @throws {Error} HY093 for mixed binding modes or invalid slot numbers;
	 * HYC00 for unsupported @name or $name placeholders.
	 */
	parse(query) {
		const names = new Map(), required = new Set(), tokens = [];
		let sql = "", count = 0, mode = "";
		// Consume SQL literals, identifiers and comments before recognizing binds.
		for(let i = 0; i < query.length;)
		{
			const start = i, c = query[i++];
			if(c === "'" || c === '"' || c === '\x60' || c === '[')
			{
				const end = c === '[' ? ']' : c;
				while(i < query.length)
				{
					if(query[i++] === end)
					{
						if(query[i] === end)
						{
							i++;
							continue;
						}
						break;
					}
				}
			} else if(c === '-' && query[i] === '-')
			{
				while(i < query.length && query[i] !== '\n') i++;
			} else if(c === '/' && query[i] === '*')
			{
				i++;
				while(i < query.length && !(query[i] === '*' && query[i + 1] === '/')) i++;
				i = Math.min(i + 2, query.length);
			} else if(c === '?' || (c === ':' && /[A-Za-z0-9_]/.test(query[i] || "")))
			{
				const named = c === ':', nextMode = named ? 'named' : 'positional';
				if(mode && mode !== nextMode) this.fail('HY093', 'Cannot mix named and positional parameters');
				mode = nextMode;
				let slot;
				if(named)
				{
					while(/[A-Za-z0-9_]/.test(query[i] || "")) i++;
					const name = query.slice(start, i);
					if(!names.has(name)) names.set(name, names.size);
					slot = names.get(name);
				} else
				{
					while(/[0-9]/.test(query[i] || "")) i++;
					slot = i > start + 1 ? Number(query.slice(start + 1, i)) - 1 : count;
				}
				// D1 permits 100 slots; never allocate using an unchecked SQL index.
				if(!Number.isInteger(slot) || slot < 0 || slot >= 100)
				{
					this.fail('HY093', 'D1 parameter positions must be between 1 and 100');
				}
				count = Math.max(count, slot + 1);
				required.add(slot);
				sql += '?' + (slot + 1);
				continue;
			} else if((c === '@' || c === '$') && /[A-Za-z0-9_]/.test(query[i] || ""))
			{
				this.fail('HYC00', 'Use PDO :name parameters instead of @name or $name');
			} else if(/[A-Za-z_]/.test(c))
			{
				while(/[A-Za-z0-9_]/.test(query[i] || "")) i++;
				tokens.push(query.slice(start, i).toUpperCase());
			} else if('();'.includes(c)) tokens.push(c);
			sql += query.slice(start, i);
		}
		// Identify INSERT/REPLACE after WITH and within scripts, but not in
		// strings, subqueries or CREATE TRIGGER bodies.
		let depth = 0, operation = "", trigger = false, blocks = 0, inserts = false;
		for(const token of tokens)
		{
			if(token === '(')
			{
				depth++;
				continue;
			}
			if(token === ')')
			{
				depth--;
				continue;
			}
			if(depth) continue;
			if(token === ';')
			{
				if(!blocks)
				{
					operation = "";
					trigger = false;
				}
				continue;
			}
			if(!operation || (operation === 'WITH' && ['SELECT', 'INSERT', 'REPLACE', 'UPDATE', 'DELETE'].includes(token)))
			{
				operation = token;
				if(operation === 'INSERT' || operation === 'REPLACE') inserts = true;
			}
			if(operation === 'CREATE' && token === 'TRIGGER') trigger = true;
			if(trigger && (token === 'BEGIN' || token === 'CASE')) blocks++;
			if(trigger && token === 'END') blocks--;
		}
		return { sql, count, mode, names, required, inserts };
	}
	, /**
	 * Clears collected parameters and buffered results while keeping the
	 * prepared query and its slot metadata available for another execution.
	 * @param {Cfd1Statement} data Statement state to clear in place.
	 * @returns {void}
	 */
	reset(data) {
		data.params = [];
		data.rows = [];
		data.columns = [];
		data.changes = 0;
	}
	, /**
	 * Validates collected slots and binds a dense parameter snapshot. Unused
	 * numbered gaps become null; a query with no slots uses its prepared object
	 * directly. This helper does not execute the statement.
	 * @param {Cfd1Statement} data Statement with values collected by C.
	 * @returns {object} D1 statement ready for run() or inclusion in batch().
	 * @throws {Error} HY093 for missing/extra values, or a D1 bind() error.
	 */
	bind(data) {
		for(const slot of data.required)
		{
			if(!Object.prototype.hasOwnProperty.call(data.params, slot))
			{
				this.fail('HY093', 'Missing value for parameter ' + (slot + 1));
			}
		}
		if(data.params.length > data.count) this.fail('HY093', 'Too many bound parameters');
		const params = Array.from({ length: data.count }, (_, i) => i in data.params ? data.params[i] : null);
		return params.length ? data.prepared.bind(...params) : data.prepared;
	}
	, /**
	 * Checks a D1 response and normalizes its rows, observed columns, and change
	 * count before publication. Accepts null, strings, finite numbers, and byte
	 * arrays as column values; does not mutate any statement state.
	 * @param {*} result Raw response from run() or one entry from batch().
	 * @returns {Cfd1Result} Validated data ready to publish.
	 * @throws {Error} An unsuccessful response, invalid change count, row, or value.
	 */
	result(result) {
		if(!result || result.success !== true) throw new Error(result && result.error || 'D1 query failed');
		const rows = result.results || [], changes = result.meta && result.meta.changes || 0;
		if(!Array.isArray(rows) || !Number.isSafeInteger(changes) || changes < 0) throw new Error('Invalid D1 result');
		for(const row of rows)
		{
			if(!row || typeof row !== 'object' || Array.isArray(row)) throw new Error('Invalid D1 row');
			for(const value of Object.values(row))
			{
				if(value === null || typeof value === 'string' || (typeof value === 'number' && Number.isFinite(value))) continue;
				if(Array.isArray(value) && value.every(byte => Number.isInteger(byte) && byte >= 0 && byte <= 255)) continue;
				throw new Error('Invalid D1 column value');
			}
		}
		return { rows, columns: rows.length ? Object.keys(rows[0]) : [], changes, meta: result.meta || {} };
	}
	, /**
	 * Publishes validated results and updates the owning connection's insert ID
	 * only for a recognized insert that changed rows. Missing or unsafe IDs are
	 * recorded as null, leaving C to report that the ID is unavailable.
	 * @param {Cfd1Statement} data Destination statement state.
	 * @param {Cfd1Result} result Response already validated by result().
	 * @returns {void}
	 */
	publish(data, result) {
		data.rows = result.rows;
		data.columns = result.columns;
		data.changes = result.changes;
		if(data.inserts && result.changes > 0)
		{
			const connection = this.connections.get(data.connection), id = result.meta.last_row_id;
			connection.lastId = Number.isSafeInteger(id) ? String(id) : null;
		}
	}
};
