Module['__pdoCfd1'] = {
	statements: new Map()
	, connections: new Map()
	, fail(sqlstate, message) {
		const error = new Error(message);
		error.sqlstate = sqlstate;
		throw error;
	}
	, error(error) {
		const state = error && error.sqlstate || 'HY000';
		const message = state + ':' + (error && error.message || String(error));
		const size = lengthBytesUTF8(message) + 1, pointer = _malloc(size);
		stringToUTF8(message, pointer, size);
		return pointer;
	}
	, parse(query) {
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
	, reset(data) {
		data.params = [];
		data.rows = [];
		data.columns = [];
		data.changes = 0;
	}
	, bind(data) {
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
	, result(result) {
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
	, publish(data, result) {
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
