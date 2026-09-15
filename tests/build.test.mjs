import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const inputs = fs.readdirSync(root).filter(name => /^pdo_cfd1_[a-z_]+\.js$/.test(name));

function fixture(t, outOfTree = false)
{
	const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'pdo-cfd1-make-'));
	t.after(() => fs.rmSync(temporary, { recursive: true, force: true }));
	const source = path.join(temporary, 'source');
	const build = outOfTree ? path.join(temporary, 'build') : source;
	fs.mkdirSync(source);
	fs.mkdirSync(build, { recursive: true });
	for(const name of ['Makefile.frag', 'pdo_cfd1_js.h.in', ...inputs])
		fs.copyFileSync(path.join(root, name), path.join(source, name));
	// PHP_ADD_MAKEFILE_FRAGMENT substitutes these paths during configure.
	const fragment = fs.readFileSync(path.join(source, 'Makefile.frag'), 'utf8')
		.replaceAll('$(srcdir)', source).replaceAll('$(builddir)', build);
	const header = path.join(build, 'generated/pdo_cfd1_js.h');
	const object = path.join(build, 'pdo_cfd1.lo');
	fs.writeFileSync(path.join(build, 'Makefile'), `all: ${object}
${object}:
\t@cat "${header}" > "$@"
${fragment}`);
	const run = (goal = 'all', success = true) => {
		const result = spawnSync('make', ['--no-print-directory', '-j8', goal], { cwd: build, encoding: 'utf8' });
		if(success) assert.equal(result.status, 0, result.stdout + result.stderr);
		else assert.notEqual(result.status, 0, 'Expected Make to reject missing JS');
		return result;
	};
	const read = () => fs.readFileSync(header, 'utf8');
	const ageOutputs = () => {
		const earlier = new Date(Date.now() - 5000);
		for(const file of [header, object]) fs.utimesSync(file, earlier, earlier);
	};
	return { source, build, header, object, run, read, ageOutputs };
}

for(const outOfTree of [false, true])
{
	test(`Make embeds JS before object compilation (${outOfTree ? 'separate build directory' : 'source directory'})`, t => {
		const f = fixture(t, outOfTree);
		f.run();
		const header = f.read();
		assert.doesNotMatch(header, /^#include/m);
		assert.equal([...header.matchAll(/^EM_JS\(/gm)].length, 4);
		assert.equal([...header.matchAll(/^EM_ASYNC_JS\(/gm)].length, 2);
		for(const name of inputs)
			assert.ok(header.includes(fs.readFileSync(path.join(f.source, name), 'utf8')), name);
		assert.equal(fs.readFileSync(f.object, 'utf8'), header);
		const mtimes = [f.header, f.object].map(file => fs.statSync(file, { bigint: true }).mtimeNs);
		f.run();
		assert.deepEqual([f.header, f.object].map(file => fs.statSync(file, { bigint: true }).mtimeNs), mtimes);

		// An edit to any included body must regenerate the header and its object.
		for(const name of inputs)
		{
			f.ageOutputs();
			const marker = `// changed ${name}: $value, \\n, "quotes", \`template\`\n`;
			fs.appendFileSync(path.join(f.source, name), marker);
			f.run();
			assert.ok(f.read().includes(marker));
			assert.equal(fs.readFileSync(f.object, 'utf8'), f.read());
		}
		f.run('clean');
		assert.ok(!fs.existsSync(f.header));
		f.run();
		assert.equal(fs.readFileSync(f.object, 'utf8'), f.read());
	});
}

test('Make discovers new includes and rejects missing inputs without replacing the last header', t => {
	const f = fixture(t, true);
	f.run();
	f.ageOutputs();
	const extra = path.join(f.source, 'pdo_cfd1_extra.js');
	fs.writeFileSync(extra, '// additional body\n');
	fs.appendFileSync(path.join(f.source, 'pdo_cfd1_js.h.in'), '#include "pdo_cfd1_extra.js"\n');
	f.run();
	assert.ok(f.read().includes('// additional body\n'));
	f.ageOutputs();
	fs.appendFileSync(extra, '// changed additional body\n');
	f.run();
	assert.ok(f.read().includes('// changed additional body\n'));
	const header = f.read();
	fs.unlinkSync(extra);
	const result = f.run('all', false);
	assert.match(result.stderr, /pdo_cfd1_extra\.js/);
	assert.equal(f.read(), header);
	assert.ok(!fs.existsSync(f.header + '.tmp'));
});
