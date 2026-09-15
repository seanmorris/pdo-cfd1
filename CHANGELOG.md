# Changes

## Unreleased

- Retain batch statements while PHP stream callbacks collect BLOB data, and release them on success and failure.
- Add named/numbered parameters while preserving ordinary execute(array) and explicit bindings.
- Add direct execution, SQLite quoting, connection-local insert IDs, binary strings/streams, buffered scroll cursors, and observed column metadata.
- Add cfd1Batch() for atomic execution of existing bound PDO statements, with shared conversion, validation, result handling, and PDO errors.
- Register the batch method without PHP 8.5's deprecated driver-method hook; report unavailable D1 server-version information explicitly.

- Adopt the driver implementation previously carried in php-wasm's PDO-CFD1 compatibility patch, preserving its compiled source behavior.
- Support the PHP 8.0–8.5 PDO callback signatures and declare the Vrzno dependency during configuration.
- Validate D1 bindings, support positional prepared queries and rebinding, and surface synchronous and asynchronous D1 failures through PDO errors.
- Reject unsupported APIs and parameter forms explicitly; clear failed result state before subsequent executions.
- Test the driver's JavaScript glue directly from its C source in this repository's CI.
