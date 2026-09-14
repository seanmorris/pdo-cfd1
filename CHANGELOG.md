# Changes

## Unreleased

- Adopt the driver implementation previously carried in php-wasm's PDO-CFD1 compatibility patch, preserving its compiled source behavior.
- Support the PHP 8.0–8.5 PDO callback signatures and declare the Vrzno dependency during configuration.
- Validate D1 bindings, support positional prepared queries and rebinding, and surface synchronous and asynchronous D1 failures through PDO errors.
- Reject unsupported APIs and parameter forms explicitly; clear failed result state before subsequent executions.
- Test the driver's JavaScript glue directly from its C source in this repository's CI.
