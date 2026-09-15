const data = Module['__pdoCfd1'].statements.get(statement);
if(name) return data.names.has(UTF8ToString(name)) ? data.names.get(UTF8ToString(name)) : -1;
return data.mode !== 'named' && position >= 0 && position < data.count ? position : -1;
