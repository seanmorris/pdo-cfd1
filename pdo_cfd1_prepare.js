const state = Module['__pdoCfd1'];
try {
    const parsed = state.parse(UTF8ToString(sql, length));
    const data = { ...parsed, connection, prepared: state.connections.get(connection).binding.prepare(parsed.sql) };
    state.reset(data);
    state.statements.set(statement, data);
    return 0;
} catch (error) { return state.error(error); }
