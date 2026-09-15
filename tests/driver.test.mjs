import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import { test } from 'node:test';

// Exercise the JavaScript bodies compiled from this checkout. PHP/Asyncify and
// real D1 integration coverage lives in php-wasm/test/cloudflare.
const source = ['pdo_cfd1.c', 'pdo_cfd1_js.h'].map(name => fs.readFileSync(new URL('../' + name, import.meta.url), 'utf8')).join('\n');

/**
 * Loads the shipped JavaScript glue with a bounded, fake Wasm string heap.
 * @param {object} binding Mock D1 binding used by this isolated unit test.
 * @returns {object} Glue functions and mock state controls.
 */
function fixture(binding = { prepare: query => ({ run: async () => ({ success: true, results: [{ query }], meta: { changes: 0 } }) }) })
{
	const strings = new Map();
	let next = 1;
	const save = value => { const pointer = next++; strings.set(pointer, value); return pointer; };
	const Module = { cfd1: { mainDb: binding } };
	const heap = new Uint32Array(1024);
	const context = vm.createContext({
		Module, Map, Set, Math, Object, Array, Number, String, Error, HEAPU32: heap
		, UTF8ToString: (pointer, length) => length === undefined ? strings.get(pointer) : strings.get(pointer).slice(0, length)
		, lengthBytesUTF8: value => Buffer.byteLength(value)
		, _malloc: () => next++
		, stringToUTF8: (value, pointer) => strings.set(pointer, value)
	});
	const functions = {};
	const args = {
		cfd1_js_init: [], cfd1_js_connect: ['connection', 'name'],
		cfd1_js_prepare: ['statement', 'connection', 'sql', 'length'],
		cfd1_js_parameter_index: ['statement', 'name', 'position'],
		cfd1_js_execute: ['statement'], cfd1_js_batch: ['connection', 'statements', 'count']
	};
	for(const [name, parameters] of Object.entries(args))
	{
		const marker = source.indexOf(', ' + name + ',');
		assert.ok(marker >= 0, name);
		const start = source.indexOf('{\n', marker) + 2;
		const end = source.indexOf('\n});', start);
		const body = source.slice(start, end);
		functions[name] = vm.runInContext('(' + (['cfd1_js_execute', 'cfd1_js_batch'].includes(name) ? 'async ' : '') + 'function(' + parameters.join(',') + '){\n' + body + '\n})', context);
	}
	functions.cfd1_js_init();
	const name = save('mainDb');
	const connection = 100;
	assert.equal(functions.cfd1_js_connect(connection, name), 0);
	const prepare = (sql, statement = 1) => functions.cfd1_js_prepare(statement, connection, save(sql), sql.length);
	const execute = (parameters, statement = 1) => {
		Module.__pdoCfd1.statements.get(statement).params = parameters;
		return functions.cfd1_js_execute(statement);
	};
	const error = pointer => strings.get(pointer);
	const batch = (ids, parameters) => {
		heap.set(ids, 0);
		ids.forEach((id, i) => { Module.__pdoCfd1.statements.get(id).params = parameters[i]; });
		return functions.cfd1_js_batch(connection, 0, ids.length);
	};
	return { Module, functions, prepare, execute, batch, error, save, name, connection };
}

test('D1 JavaScript glue parses without eval and rejects missing/malformed bindings', () => {
    assert.doesNotMatch(source, /\beval\s*\(|new\s+Function\s*\(/);
    const f = fixture();
    assert.equal(f.functions.cfd1_js_connect(f.connection, f.name), 0);
    assert.match(f.error(f.functions.cfd1_js_connect(200, f.save('missing'))), /^HY000:Missing or invalid D1 binding/);
    f.Module.cfd1.mainDb = {};
    assert.match(f.error(f.functions.cfd1_js_connect(200, f.name)), /^HY000:Missing or invalid D1 binding/);
});

test('PHP 8.0 ABI adapters and batch driver method are registered', () => {
    assert.match(source, /\.doer = cfd1_doer/);
    assert.match(source, /static zend_long cfd1_doer\(pdo_dbh_t \*dbh, const char \*sql, size_t length\)/);
    assert.match(source, /static zend_long cfd1_doer\(pdo_dbh_t \*dbh, const zend_string \*query\)/);
    assert.match(source, /\.quoter = cfd1_quoter/);
    assert.match(source, /PHP_ME\(PDO, cfd1Batch/);
});

test('SQL scanner ignores placeholders in literals, quoted identifiers and comments', async () => {
    const f = fixture({ prepare: () => ({ bind: (...params) => ({ run: async () => ({ success: true, results: [{ n: params.length }] }) }) }) });
    assert.equal(f.prepare("SELECT ?, '?', \"?\", `?`, [?], 'it''s ?' -- :ignored ?\n /* @ignored ? */ , ?"), 0);
    assert.equal(f.Module.__pdoCfd1.statements.get(1).count, 2);
    assert.equal(await f.execute([1, 2]), 0);
    for(const sql of ['SELECT @named', 'SELECT $named'])
    {
        assert.match(f.error(f.prepare(sql)), /^HYC00:/);
    }
});

test('prepared execution rebinds each call and exposes results and D1 changes', async () => {
    const calls = [];
    const f = fixture({ prepare: () => ({
        bind: (...params) => {
            calls.push(params);
            return { run: async () => ({ success: true, results: [{ value: params[0] }], meta: { changes: 1 } }) };
        }
    }) });
    assert.equal(f.prepare('SELECT ?'), 0);
    assert.equal(await f.execute([null]), 0);
    assert.equal(f.Module.__pdoCfd1.statements.get(1).rows[0].value, null);
    assert.equal(await f.execute(['next']), 0);
    assert.equal(f.Module.__pdoCfd1.statements.get(1).rows[0].value, 'next');
    assert.equal(f.Module.__pdoCfd1.statements.get(1).changes, 1);
    assert.deepEqual(calls, [[null], ['next']]);
    assert.equal(f.Module.__pdoCfd1.statements.get(1).params.length, 0);
});

test('async rejection and unsuccessful D1 results are returned as errors, with clean retry state', async () => {
    let attempt = 0;
    const f = fixture({ prepare: () => ({ run: async () => {
        await Promise.resolve();
        if(attempt++ === 0) throw new Error('rejected query');
        if(attempt === 2) return { success: false, error: 'unsuccessful query' };
        return { success: true, results: [{ ok: 1 }], meta: { changes: 0 } };
    } }) });
    f.prepare('SELECT 1');
    assert.match(f.error(await f.execute([])), /^HY000:rejected query/);
    assert.equal(f.Module.__pdoCfd1.statements.get(1).rows.length, 0);
    assert.match(f.error(await f.execute([])), /^HY000:unsuccessful query/);
    assert.equal(await f.execute([]), 0);
    assert.equal(f.Module.__pdoCfd1.statements.get(1).rows[0].ok, 1);
});

test('parameter count and sparse bindings fail without running a query', async () => {
    const f = fixture({ prepare: () => ({ run: async () => { throw new Error('must not run'); } }) });
    f.prepare('SELECT ?, ?');
    assert.match(f.error(await f.execute([1])), /^HY093:/);
    assert.match(f.error(await f.execute([1, 2, 3])), /^HY093:/);
    const sparse = [];
    sparse[1] = 2;
    assert.match(f.error(await f.execute(sparse)), /^HY093:/);
});

test('synchronous prepare/bind exceptions are captured too', async () => {
    const f = fixture({ prepare: () => { throw new Error('invalid SQL'); } });
    assert.match(f.error(f.prepare('bad SQL')), /^HY000:invalid SQL/);
    f.Module.cfd1.mainDb = { prepare: () => ({ bind: () => { throw new Error('invalid value'); } }) };
    assert.equal(f.functions.cfd1_js_connect(f.connection, f.name), 0);
    assert.equal(f.prepare('SELECT ?'), 0);
    assert.match(f.error(await f.execute([1])), /^HY000:invalid value/);
});

test('named parameters are rewritten once per unique name without interpolating values', async () => {
    const calls = [];
    const f = fixture({ prepare: sql => ({ bind: (...params) => ({ run: async () => {
        calls.push({ sql, params });
        return { success: true, results: [{ value: params[0] }] };
    } }) }) });
    assert.equal(f.prepare("SELECT :name, :name, :other, ':unchanged' -- :comment"), 0);
    const data = f.Module.__pdoCfd1.statements.get(1);
    assert.equal(data.sql, "SELECT ?1, ?1, ?2, ':unchanged' -- :comment");
    assert.equal(data.count, 2);
    assert.equal(f.functions.cfd1_js_parameter_index(1, f.save(':other'), -1), 1);
    assert.equal(f.functions.cfd1_js_parameter_index(1, 0, 0), -1);
    assert.equal(f.functions.cfd1_js_parameter_index(1, f.save(':missing'), -1), -1);
    assert.equal(await f.execute(["'; DROP TABLE users; --", null]), 0);
    assert.deepEqual(calls[0].params, ["'; DROP TABLE users; --", null]);
    assert.equal(await f.execute(['second', 2]), 0);
    assert.equal(calls[1].params[0], 'second');
});

test('numbered parameters preserve SQLite slot order and permit unused gaps', async () => {
    const calls = [];
    const f = fixture({ prepare: sql => ({ bind: (...params) => ({ run: async () => {
        calls.push({ sql, params });
        return { success: true, results: [] };
    } }) }) });
    assert.equal(f.prepare('SELECT ?3, ?, ?3, ?1'), 0);
    assert.equal(f.Module.__pdoCfd1.statements.get(1).sql, 'SELECT ?3, ?4, ?3, ?1');
    const params = [];
    params[0] = 'first'; params[2] = 'third'; params[3] = 'fourth';
    assert.equal(await f.execute(params), 0);
    assert.deepEqual(calls[0].params, ['first', null, 'third', 'fourth']);
    assert.match(f.error(await f.execute(['first'])), /^HY093:/);
    assert.equal(calls.length, 1);
    for (const sql of ['SELECT :name, ?', 'SELECT ?1, :name', 'SELECT ?0', 'SELECT ?101', 'SELECT ?99999999999999999999']) {
        assert.match(f.error(f.prepare(sql)), /^HY093:/);
    }
});

test('insert recognition excludes SQL text, subqueries and trigger bodies', () => {
    const f = fixture(), parse = sql => f.Module.__pdoCfd1.parse(sql).inserts;
    for (const sql of [
        'INSERT INTO t VALUES (1)', 'REPLACE INTO t VALUES (1)',
        'WITH x AS (SELECT 1) INSERT INTO t SELECT * FROM x',
        'CREATE TABLE t(x); INSERT INTO t VALUES (1); SELECT * FROM t',
        'WITH RECURSIVE x(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM x WHERE n<3) INSERT INTO t SELECT * FROM x'
    ]) assert.equal(parse(sql), true, sql);
    for (const sql of [
        "SELECT 'INSERT INTO t VALUES (1)'", 'WITH x AS (SELECT 1) SELECT * FROM x',
        'CREATE TRIGGER tr AFTER UPDATE ON t BEGIN INSERT INTO log VALUES(1); INSERT INTO log VALUES(2); END;',
        "CREATE TRIGGER tr AFTER UPDATE ON t BEGIN SELECT CASE WHEN 1 THEN 'END' ELSE 'INSERT' END; INSERT INTO log VALUES(2); END;",
        'UPDATE t SET x = (SELECT 1)', '/* INSERT */ DELETE FROM t'
    ]) assert.equal(parse(sql), false, sql);
});

test('last insert IDs belong to each connection and never come from reads or failed writes', async () => {
    let result = { success: true, results: [], meta: { changes: 1, last_row_id: 4294967297 } };
    const f = fixture({ prepare: () => ({ run: async () => result }) });
    assert.equal(f.prepare('INSERT INTO t VALUES (1)'), 0);
    assert.equal(await f.execute([]), 0);
    const connection = f.Module.__pdoCfd1.connections.get(f.connection);
    assert.equal(connection.lastId, '4294967297');
    f.prepare('SELECT 1', 2);
    result = { success: true, results: [{ x: 1 }], meta: { changes: 0, last_row_id: 99 } };
    assert.equal(await f.execute([], 2), 0);
    assert.equal(connection.lastId, '4294967297');
    result = { success: false, error: 'write failed' };
    assert.match(f.error(await f.execute([])), /^HY000:/);
    assert.equal(connection.lastId, '4294967297');
    assert.equal(f.functions.cfd1_js_connect(200, f.name), 0);
    assert.equal(f.Module.__pdoCfd1.connections.get(200).lastId, '0');
    result = { success: true, results: [], meta: { changes: 1, last_row_id: Number.MAX_SAFE_INTEGER + 1 } };
    assert.equal(await f.execute([]), 0, 'a committed write still succeeds when its ID cannot be represented');
    assert.equal(connection.lastId, null);
    assert.equal(f.Module.__pdoCfd1.connections.get(200).lastId, '0');
});

test('batch binds every statement once and publishes independent results without calling run', async () => {
    let calls = 0;
    const f = fixture({
        prepare: sql => ({ sql, bind: (...params) => ({ sql, params }), run: () => { throw new Error('must use batch'); } }),
        batch: async statements => {
            calls++;
            assert.equal(statements.length, 2);
            assert.deepEqual(statements[0].params, [new Uint8Array([0, 255])]);
            assert.deepEqual(statements[1].params, ['key']);
            return [
                { success: true, results: [], meta: { changes: 1, last_row_id: 12 } },
                { success: true, results: [{ value: [0, 255] }], meta: { changes: 0, last_row_id: 999 } }
            ];
        }
    });
    f.prepare('INSERT INTO t VALUES (?)', 1);
    f.prepare('SELECT * FROM t WHERE key = :key', 2);
    assert.equal(await f.batch([1, 2], [[new Uint8Array([0, 255])], ['key']]), 0);
    assert.equal(calls, 1);
    const state = f.Module.__pdoCfd1;
    assert.equal(state.connections.get(f.connection).lastId, '12');
    assert.equal(state.statements.get(1).changes, 1);
    assert.deepEqual(state.statements.get(2).rows, [{ value: [0, 255] }]);
    assert.equal(state.statements.get(1).params.length, 0);
    assert.equal(state.statements.get(2).params.length, 0);
});

test('batch parameter failures prevent the entire D1 request', async () => {
    let calls = 0;
    const f = fixture({ prepare: () => ({ bind: (...params) => ({ params }) }), batch: () => { calls++; } });
    f.prepare('INSERT INTO t VALUES (?)', 1); f.prepare('SELECT ?', 2);
    assert.match(f.error(await f.batch([1, 2], [[1], []])), /^HY093:/);
    assert.equal(calls, 0);
    assert.equal(f.Module.__pdoCfd1.statements.get(1).rows.length, 0);
});

test('batch failures clear every result without publishing partial insert IDs or retrying', async () => {
    let response, calls = 0;
    const f = fixture({ prepare: () => ({}), batch: async () => {
        calls++;
        if (response instanceof Error) throw response;
        return response;
    } });
    f.prepare('INSERT INTO t VALUES (1)', 1); f.prepare('SELECT 1', 2);
    const state = f.Module.__pdoCfd1;
    state.connections.get(f.connection).lastId = '7';
    for (const failure of [
        new Error('transaction rolled back'),
        [{ success: true, meta: { changes: 1, last_row_id: 99 } }, { success: false, error: 'later statement failed' }],
        [{ success: true }],
        [{ success: true, meta: { changes: 1, last_row_id: 99 } }, { success: true, results: [{ invalid: {} }] }]
    ]) {
        response = failure;
        assert.match(f.error(await f.batch([1, 2], [[], []])), /^HY000:/);
        for (const id of [1, 2]) assert.equal(state.statements.get(id).rows.length, 0);
        assert.equal(state.connections.get(f.connection).lastId, '7');
    }
    assert.equal(calls, 4);
    response = [{ success: true, meta: { changes: 1, last_row_id: 8 } }, { success: true, results: [{ x: 8 }] }];
    assert.equal(await f.batch([1, 2], [[], []]), 0);
    assert.equal(state.connections.get(f.connection).lastId, '8');
});

test('prepare-only bindings support ordinary execution while rejecting batch explicitly', async () => {
    const f = fixture();
    f.prepare('SELECT 1');
    assert.equal(await f.execute([]), 0);
    assert.match(f.error(await f.batch([1], [[]])), /^HYC00:/);
    assert.equal(await f.execute([]), 0);
});
