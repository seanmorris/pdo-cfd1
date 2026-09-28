# pdo-cfd1

PDO driver for Cloudflare D1 and php-wasm.

PDO-CFD1 connects PHP's PDO interface to a Worker's D1 bindings. It supports PHP
8.0 through PHP 8.5 running under Emscripten. The private `pdo-cfd1-driver` npm
package supplies this repository's JavaScript build and test tools.

## Table of Contents

- [Install](#install)
- [Usage](#usage)
- [Building](#building)
- [API](#api)
	- [Prepared parameters](#prepared-parameters)
	- [Execution and affected rows](#execution-and-affected-rows)
	- [Quoting and binary values](#quoting-and-binary-values)
	- [Insert IDs](#insert-ids)
	- [Cursors and metadata](#cursors-and-metadata)
	- [Attributes](#attributes)
	- [Atomic batches](#atomic-batches)
	- [Errors and limits](#errors-and-limits)
- [Maintainers](#maintainers)
- [Contributing](#contributing)
- [License](#license)

## Install

Get a `php-cloud-wasm` artifact from the nightly distribution described in the
[Cloudflare guide](https://github.com/seanmorris/php-wasm/blob/develop/CLOUDFLARE.md#ci-and-nightly-artifacts),
or build one from a php-wasm checkout with its builder image available:

```sh
npm ci
make cloudflare-mjs PHP_VERSION=8.3
```

The Cloudflare profile includes PDO-CFD1 and Vrzno. Copy the complete artifact for
PHP 8.3 into your Worker project's `php-cloud-wasm/` directory. The manifest
`php8.3-cloudflare.manifest.json` lists the entrypoint, runtime, Wasm, and helpers
that belong together. For a local build, those files are in
`packages/php-cloud-wasm/`.

### Dependencies

Source builds require Emscripten with Asyncify enabled, PDO, Vrzno, and GNU Make
4.3 or newer. Install Wrangler 4 in the Worker project using Node.js and npm:

```sh
npm install --save-dev wrangler@4
```

### Configure the Worker

Save this as `wrangler.toml` beside `worker.mjs` and `php-cloud-wasm/`:

```toml
name = "pdo-cfd1-example"
main = "worker.mjs"
compatibility_date = "2025-05-05"
no_bundle = true

[[rules]]
type = "ESModule"
globs = ["php-cloud-wasm/*.mjs"]
fallthrough = false

[[rules]]
type = "CompiledWasm"
globs = ["php-cloud-wasm/*.wasm"]
fallthrough = false

[[d1_databases]]
binding = "DB"
database_name = "pdo-cfd1-example"
database_id = "00000000-0000-0000-0000-000000000000"
```

The database ID above is a local placeholder; use your D1 database's ID for
deployment. See [local D1 development](https://developers.cloudflare.com/d1/best-practices/local-development/).
The [module rules](https://developers.cloudflare.com/workers/wrangler/configuration/#bundling)
load the generated JavaScript and compiled Wasm without bundling. The compatibility
date enables Vrzno's required `WeakRef` and `FinalizationRegistry`; older dates
need `compatibility_flags = ["enable_weak_ref"]`.

## Usage

Save this as `schema.sql` in the Worker project:

```sql
CREATE TABLE IF NOT EXISTS users (id INTEGER PRIMARY KEY, name TEXT NOT NULL);
INSERT OR IGNORE INTO users (id, name) VALUES (1, 'Alice');
```

Save this as `worker.mjs` to query the user and return the row as JSON:

```js
import { PhpCloudflare } from './php-cloud-wasm/php8.3-cloudflare.mjs';

export default {
	async fetch(request, env) {
		const php = new PhpCloudflare({cfd1: {mainDb: env.DB}});
		let output = '';
		php.addEventListener('output', event => { output += event.detail.join(''); });
		php.addEventListener('error', event => console.error(event.detail.join('')));

		const exitCode = await php.run(`<?php
			$pdo = new PDO('cfd1:mainDb', null, null, [
				PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION
			]);
			$select = $pdo->prepare('SELECT name FROM users WHERE id = ?');
			$select->execute([1]);
			echo json_encode($select->fetch(PDO::FETCH_ASSOC));
		`);

		if(exitCode !== 0)
		{
			return new Response('Query failed', {status: 500});
		}

		return new Response(output, {
			headers: {'content-type': 'application/json'}
		});
	}
};
```

The `cfd1` map connects `env.DB` to the PHP DSN `cfd1:mainDb`. Add more keys to
expose other D1 bindings. Construct PHP inside the request handler so the instance
and binding map belong to that request.

Load the local schema, then start the Worker:

```sh
npx wrangler d1 execute pdo-cfd1-example --local --file schema.sql
npx wrangler dev --local
```

In another terminal:

```sh
curl http://127.0.0.1:8787/
```

The response is `{"name":"Alice"}`.

## Building

Direct configuration uses `--enable-pdo-cfd1`; [config.m4](config.m4) declares the
dependencies. The php-wasm importer uses these settings:

| Setting | Effect |
| --- | --- |
| `WITH_PDO_CFD1=1` | Enables the driver. |
| `WITH_VRZNO=1` | Enables its Vrzno dependency. |
| `PDO_CFD1_REPOSITORY` | Selects the upstream source repository. |
| `PDO_CFD1_REF` | Selects the upstream source revision; pin a commit for reproducible builds. |
| `PDO_CFD1_DEV_PATH` | Uses an explicit local checkout instead of upstream sources. |

For a local driver checkout, add `PDO_CFD1_DEV_PATH=/absolute/path/to/pdo-cfd1` to
the Make command in [Install](#install). Other build profiles need both extension
flags enabled.

## API

The examples below assume a `$pdo` connection created as in [Usage](#usage).

### Prepared parameters

Ordinary execution accepts parameter arrays; explicit binding is optional:

```php
$stmt = $pdo->prepare('SELECT name FROM users WHERE id = ?');
$stmt->execute([1]);

$stmt = $pdo->prepare('SELECT name FROM users WHERE id = :id');
$stmt->execute(['id' => 1]);
$user = $stmt->fetch(PDO::FETCH_ASSOC);
```

Bare `?`, numbered `?NNN`, and PDO-style `:name` placeholders are supported.
Repeated names share one value, and named array keys may include the leading
colon. Choose either named or positional placeholders for a statement.

Numeric `bindValue()` and `bindParam()` positions start at 1; numeric
`execute([...])` keys start at 0. Numbered placeholders keep SQLite's slot
numbering, including gaps. Missing referenced slots, extra bindings, and indices
outside 1–100 fail before execution.

`execute([...])` uses PDO's default parameter type, `PDO::PARAM_STR`. Explicit
binding selects `PDO::PARAM_INT`, `PDO::PARAM_BOOL`, `PDO::PARAM_NULL`, or
`PDO::PARAM_LOB`. Bound references are read and converted again on each execution.

### Execution and affected rows

`query()`, `exec()`, repeated execution, normal PDO fetch modes, and re-execution
after `closeCursor()` are supported. `exec()` accepts SQL scripts supported by D1
and returns D1's affected-row count. Write `rowCount()` uses `meta.changes`;
it does not count SELECT results.

### Quoting and binary values

`quote()` escapes SQLite text literals. `quote($bytes, PDO::PARAM_LOB)` produces a
hexadecimal BLOB literal. Quoted text containing NUL bytes fails; bound text can
contain them.

Bind a PHP binary string or readable stream with `PDO::PARAM_LOB`. Streams are
read from their current position at execution; rewind before reuse as needed.
Empty BLOBs and SQL NULL remain distinct. Fetched BLOBs are binary PHP strings,
with no UTF-8 decoding.

### Insert IDs

`lastInsertId()` returns the last successful insert's D1 `meta.last_row_id` as a
string. Each connection caches its own ID, initially `"0"`. Reads, updates, other
connections, and failed executions leave it unchanged.

IDs apply to inserts into rowid tables; named sequences are unsupported. When
D1 omits an insert ID or returns one outside JavaScript's safe integer range,
the write succeeds but `lastInsertId()` reports an error. Use
`RETURNING CAST(id AS TEXT)` to retrieve a wide ID without number conversion.

### Cursors and metadata

Results are buffered in the Worker. Forward-only cursors are the default. Pass
`[PDO::ATTR_CURSOR => PDO::CURSOR_SCROLL]` to `prepare()` for NEXT, PRIOR, FIRST,
LAST, ABS, and REL fetch orientations. ABS offsets are zero-based; REL offsets
start from the current row. Moving outside the result returns `false`; a later
move can return to an existing row.

`getColumnMeta()` reports names and types observed in returned values. It uses
the current row, or the first row before fetching. Table origins, declared SQL
types, and schema flags are unavailable. Empty results have no column metadata
through D1's result interface. Use distinct aliases because D1's object results
do not preserve duplicate column names.

### Attributes

PDO error modes, case conversion, default fetch modes, and bound columns retain
their normal behavior. Driver name, client version, cursor mode, autocommit, and
native-prepare attributes are available. Autocommit must stay enabled and emulated
prepares disabled. D1 does not expose its SQLite server version, so
`PDO::ATTR_SERVER_VERSION` fails explicitly.

### Atomic batches

Bind parameters before passing existing statements to `cfd1Batch()`:

```php
$insert = $pdo->prepare('INSERT INTO users (name) VALUES (:name)');
$insert->bindValue('name', 'Bob');
$select = $pdo->prepare('SELECT name FROM users ORDER BY id');

$pdo->cfd1Batch([$insert, $select]);
$users = $select->fetchAll(PDO::FETCH_ASSOC);
$inserted = $insert->rowCount();
```

`cfd1Batch(array $statements): bool` submits one atomic D1 batch. Supply a list of
distinct statements from the same PDO connection. The batch captures bound
values when it starts. Validation failures prevent a database request; an empty
list succeeds without one. A binding that exposes only `prepare()` still works
for ordinary queries, but a nonempty batch requires `batch()`.

On success, the method returns `true`; each statement owns its results and
affected-row count. A failed execution clears the attempted statements' previous
results and follows the connection's PDO error mode. Database statement failures
roll back the batch.
Transport failures are not retried automatically because the commit outcome may
be unknown.

Statements remain reusable with `execute([...])`, explicit bindings, or another
batch. Calling `execute()` runs the statement immediately; use binding methods
to supply parameters for the later batch.

`cfd1Batch()` works on PHP 8.0–8.5 without driver-method deprecation warnings.
Calling it on an uninitialized PDO object or another driver throws `PDOException`.

### Errors and limits

D1 failures follow PDO's error mode: exception mode throws `PDOException`,
warning mode emits a warning and returns `false`, and silent mode returns `false`
with `errorCode()` / `errorInfo()`. Missing or malformed D1 bindings fail
construction. Failed executions clear earlier rows and report failure.

Choose all statements before submitting a batch. PDO transaction methods,
persistent connections, output parameters, streaming cursors, multiple
result sets, `@name` / `$name` placeholders, and unsupported attributes are
unavailable. Use the native D1 API for capabilities outside this driver. D1's
JavaScript API limits numeric precision; use BLOB bindings for binary data.

## Maintainers

[Sean Morris](https://github.com/seanmorris).

## Contributing

Use [GitHub issues](https://github.com/seanmorris/pdo-cfd1/issues) for questions and
bug reports, and [pull requests](https://github.com/seanmorris/pdo-cfd1/pulls) for
changes. Follow `sm-no-saccade-style` and run the checks below.

### Edit the bridge

JavaScript bodies live in [js/](js/). [pdo_cfd1_js.h.in](pdo_cfd1_js.h.in) retains
their C signatures and `EM_JS`/`EM_ASYNC_JS` wrappers.
[Makefile.frag](Makefile.frag) invokes `CC` with `-E -P -CC -fdirectives-only` to
expand the JavaScript includes before compiling the driver. This preserves
comments and JavaScript identifiers that resemble C macros. The JavaScript stays
inside the compiled object.

The generated header, `generated/pdo_cfd1_js.h`, has a companion dependency file,
`generated/pdo_cfd1_js.d`, covering all includes, including nested ones. Editing an
input rebuilds the header and driver. Failed generation reports the compiler's
file/line diagnostic and preserves the previous outputs. Both generated files
remain in the build directory and are excluded from source imports and commits.

To generate the header in a driver checkout:

```sh
make -f Makefile.frag CC=emcc srcdir=. builddir=. generated/pdo_cfd1_js.h
```

### Run tests

With Node.js, GNU Make 4.3 or newer, and Emscripten 6.0.6 on `PATH`, run:

```sh
npm ci
npm run lint
npm test
```

PHP's Make build needs no npm packages. Lint covers `js/`, tests, and ESLint
configuration; use the same rules for README JavaScript examples.
`npm run lint:fix` applies formatting.
[CI](.github/workflows/ci.yml) runs lint before tests on Node 22.23.2 and 24.5.0.

The JS tests cover binding validation, parameter scanning and rebinding, insert
IDs, atomic batches, and failure recovery. Make tests cover fresh and incremental
generation, parallel dependencies, separate build directories, nested includes,
macro preservation, missing inputs and dependency files, and clean targets.
The compile/link test removes sources and headers before linking the object alone
and exercising all six bridge functions, including asynchronous execution and
batching.

Run JS tests without a compiler using `node --test tests/driver.test.mjs`.
PHP/Asyncify and local D1 integration tests live in
[php-wasm/test/cloudflare](https://github.com/seanmorris/php-wasm/tree/develop/test/cloudflare).

## License

UNLICENSED. This repository has no declared license. [CREDITS](CREDITS) names
Sean Morris as its author.
