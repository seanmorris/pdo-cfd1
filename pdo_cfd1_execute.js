const state = Module['__pdoCfd1'], data = state.statements.get(statement);
try
{
	state.publish(data, state.result(await state.bind(data).run()));
	return 0;
}
catch(error)
{
	state.reset(data);
	return state.error(error);
}
finally
{
	data.params = [];
}
