/* PDO over the public D1 binding API. PHP 8.0-8.5, Emscripten and Asyncify. */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "php.h"
#include "ext/standard/info.h"
#include "ext/standard/php_string.h"
#include "ext/pdo/php_pdo_driver.h"
#include "php_pdo_cfd1.h"
#include "php_streams.h"
#include "zend_exceptions.h"
#include <emscripten.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

typedef struct { char *message; } cfd1_error_info;
typedef struct { cfd1_error_info error; } cfd1_db;
typedef struct {
    cfd1_error_info error;
    zend_long current, rows, cursor;
} cfd1_stmt;

#if PHP_VERSION_ID < 80100
# define CFD1_BOOL int
# define CFD1_VOID int
# define CFD1_RETURN_VOID return 1
#else
# define CFD1_BOOL bool
# define CFD1_VOID void
# define CFD1_RETURN_VOID return
#endif

#include <pdo_cfd1_js.h>

static cfd1_error_info *cfd1_error(pdo_dbh_t *dbh, pdo_stmt_t *stmt)
{
    return stmt ? &((cfd1_stmt *)stmt->driver_data)->error : &((cfd1_db *)dbh->driver_data)->error;
}

static void cfd1_clear_error(pdo_dbh_t *dbh, pdo_stmt_t *stmt)
{
    cfd1_error_info *error = cfd1_error(dbh, stmt);
    if (error->message) pefree(error->message, dbh->is_persistent);
    error->message = NULL;
    memcpy(stmt ? stmt->error_code : dbh->error_code, PDO_ERR_NONE, sizeof(pdo_error_type));
}

static int cfd1_fail(pdo_dbh_t *dbh, pdo_stmt_t *stmt, const char *state, const char *message)
{
    cfd1_clear_error(dbh, stmt);
    memcpy(stmt ? stmt->error_code : dbh->error_code, state, sizeof(pdo_error_type));
    cfd1_error(dbh, stmt)->message = pestrdup(message, dbh->is_persistent);
    return 0;
}

static int cfd1_js_failure(pdo_dbh_t *dbh, pdo_stmt_t *stmt, char *message)
{
    char state[6] = "HY000";
    const char *detail = message;
    if (strlen(message) > 6 && message[5] == ':') {
        memcpy(state, message, 5);
        detail = message + 6;
    }
    cfd1_fail(dbh, stmt, state, detail);
    free(message);
    return 0;
}

/* Driver-specific methods bypass PDO's ordinary callback error dispatcher. */
static void cfd1_report_error(pdo_dbh_t *dbh)
{
    if (EG(exception) || dbh->error_mode == PDO_ERRMODE_SILENT) return;
    char *message = cfd1_error(dbh, NULL)->message;
    if (dbh->error_mode == PDO_ERRMODE_EXCEPTION) {
        pdo_throw_exception(1, message, &dbh->error_code);
        zend_update_property_string(php_pdo_get_exception(), EG(exception), "code", sizeof("code") - 1, dbh->error_code);
    } else {
        php_error_docref(NULL, E_WARNING, "SQLSTATE[%s]: %s", dbh->error_code, message);
    }
}

static CFD1_VOID cfd1_fetch_error(pdo_dbh_t *dbh, pdo_stmt_t *stmt, zval *info)
{
    cfd1_error_info *error = cfd1_error(dbh, stmt);
    if (error->message) {
        add_next_index_long(info, 1);
        add_next_index_string(info, error->message);
    }
    CFD1_RETURN_VOID;
}

static int cfd1_stmt_destroy(pdo_stmt_t *stmt)
{
    cfd1_stmt *data = stmt->driver_data;
    if (!data) return 1;
    EM_ASM({ Module['__pdoCfd1'].statements.delete($0); }, stmt);
    if (data->error.message) pefree(data->error.message, stmt->dbh->is_persistent);
    efree(data);
    stmt->driver_data = NULL;
    return 1;
}

static void cfd1_stmt_reset(pdo_stmt_t *stmt)
{
    cfd1_stmt *data = stmt->driver_data;
    cfd1_clear_error(stmt->dbh, stmt);
    data->current = -1;
    data->rows = 0;
    stmt->executed = 0;
    stmt->row_count = 0;
    php_pdo_stmt_set_column_count(stmt, 0);
    EM_ASM({
        const state = Module['__pdoCfd1'];
        state.reset(state.statements.get($0));
    }, stmt);
}

static int cfd1_collect_parameters(pdo_stmt_t *stmt)
{
    struct pdo_bound_param_data *parameter;
    if (!stmt->bound_params) return 1;
    ZEND_HASH_FOREACH_PTR(stmt->bound_params, parameter) {
        zval *value = &parameter->parameter;
        ZVAL_DEREF(value);
        if (parameter->name && memchr(ZSTR_VAL(parameter->name), '\0', ZSTR_LEN(parameter->name))) {
            return cfd1_fail(stmt->dbh, stmt, "HY093", "Parameter names cannot contain NUL bytes");
        }
        int slot = cfd1_js_parameter_index(stmt,
            parameter->name ? ZSTR_VAL(parameter->name) : NULL, parameter->paramno);
        if (slot < 0) return cfd1_fail(stmt->dbh, stmt, "HY093", "Unknown parameter or incompatible binding style");
        if (parameter->param_type & PDO_PARAM_INPUT_OUTPUT) {
            return cfd1_fail(stmt->dbh, stmt, "HYC00", "Output parameters are not supported");
        }
        enum pdo_param_type type = PDO_PARAM_TYPE(parameter->param_type);
        zend_string *bytes = NULL;
        if (Z_TYPE_P(value) == IS_NULL || type == PDO_PARAM_NULL) {
            EM_ASM({ Module['__pdoCfd1'].statements.get($0).params[$1] = null; }, stmt, slot);
            continue;
        }
        if (type == PDO_PARAM_LOB) {
            if (Z_TYPE_P(value) == IS_RESOURCE) {
                php_stream *stream = NULL;
                php_stream_from_zval_no_verify(stream, value);
                if (!stream || !stream->ops->read || (stream->mode[0] != 'r' && !strchr(stream->mode, '+'))) {
                    return cfd1_fail(stmt->dbh, stmt, "HY105", "Expected a readable PHP stream");
                }
                bytes = php_stream_copy_to_mem(stream, PHP_STREAM_COPY_ALL, 0);
                if (!bytes && php_stream_eof(stream)) bytes = ZSTR_EMPTY_ALLOC();
                if (!bytes) return cfd1_fail(stmt->dbh, stmt, "HY105", "Could not read the BLOB stream");
            } else if (Z_TYPE_P(value) == IS_STRING) {
                bytes = zend_string_copy(Z_STR_P(value));
            } else {
                return cfd1_fail(stmt->dbh, stmt, "HY105", "BLOB parameters require strings or readable streams");
            }
            EM_ASM({
                Module['__pdoCfd1'].statements.get($0).params[$1] = HEAPU8.slice($2, $2 + $3);
            }, stmt, slot, ZSTR_VAL(bytes), ZSTR_LEN(bytes));
            zend_string_release(bytes);
            continue;
        }
        if (Z_TYPE_P(value) != IS_STRING && Z_TYPE_P(value) != IS_LONG && Z_TYPE_P(value) != IS_DOUBLE
            && Z_TYPE_P(value) != IS_TRUE && Z_TYPE_P(value) != IS_FALSE) {
            return cfd1_fail(stmt->dbh, stmt, "HY105", "Unsupported D1 parameter type");
        }
        if (type == PDO_PARAM_STR) {
            bytes = zval_get_string(value);
            if (EG(exception)) {
                zend_string_release(bytes);
                return cfd1_fail(stmt->dbh, stmt, "HY105", "Could not convert parameter to text");
            }
            EM_ASM({
                const bytes = HEAPU8.subarray($2, $2 + $3);
                Module['__pdoCfd1'].statements.get($0).params[$1] = new TextDecoder().decode(bytes);
            }, stmt, slot, ZSTR_VAL(bytes), ZSTR_LEN(bytes));
            zend_string_release(bytes);
        } else if (type == PDO_PARAM_INT || type == PDO_PARAM_BOOL) {
            double number = type == PDO_PARAM_BOOL ? zend_is_true(value) : zval_get_long(value);
            EM_ASM({ Module['__pdoCfd1'].statements.get($0).params[$1] = $2; }, stmt, slot, number);
        } else {
            return cfd1_fail(stmt->dbh, stmt, "HYC00", "Unsupported PDO parameter type");
        }
    } ZEND_HASH_FOREACH_END();
    return 1;
}

static void cfd1_stmt_publish(pdo_stmt_t *stmt)
{
    cfd1_stmt *data = stmt->driver_data;
    data->rows = (zend_long) EM_ASM_DOUBLE({ return Module['__pdoCfd1'].statements.get($0).rows.length; }, stmt);
    stmt->row_count = (zend_long) EM_ASM_DOUBLE({ return Module['__pdoCfd1'].statements.get($0).changes; }, stmt);
    php_pdo_stmt_set_column_count(stmt, EM_ASM_INT({ return Module['__pdoCfd1'].statements.get($0).columns.length; }, stmt));
    /* Batches bypass PDOStatement::execute(), so provide column descriptions
     * here for both paths, including metadata and named bindColumn() before fetch. */
    if (stmt->column_count) {
        stmt->columns = ecalloc(stmt->column_count, sizeof(struct pdo_column_data));
        for (int column = 0; column < stmt->column_count; column++) {
            stmt->methods->describer(stmt, column);
            zend_string *name = stmt->columns[column].name;
            if (stmt->dbh->desired_case == PDO_CASE_LOWER) {
                stmt->columns[column].name = zend_string_tolower(name);
                zend_string_release(name);
            } else if (stmt->dbh->desired_case == PDO_CASE_UPPER) {
#if PHP_VERSION_ID < 80200
                stmt->columns[column].name = php_string_toupper(name);
#else
                stmt->columns[column].name = zend_string_toupper(name);
#endif
                zend_string_release(name);
            }
            if (stmt->bound_columns) {
                struct pdo_bound_param_data *bound = zend_hash_find_ptr(stmt->bound_columns, stmt->columns[column].name);
                if (bound) bound->paramno = column;
            }
        }
    }
    stmt->executed = 1;
}

static int cfd1_stmt_execute(pdo_stmt_t *stmt)
{
    cfd1_stmt_reset(stmt);
    if (!cfd1_collect_parameters(stmt)) return 0;
    char *error = cfd1_js_execute(stmt);
    if (error) return cfd1_js_failure(stmt->dbh, stmt, error);
    cfd1_stmt_publish(stmt);
    return 1;
}

static int cfd1_stmt_fetch(pdo_stmt_t *stmt, enum pdo_fetch_orientation orientation, zend_long offset)
{
    cfd1_stmt *data = stmt->driver_data;
    if (data->cursor != PDO_CURSOR_SCROLL && orientation != PDO_FETCH_ORI_NEXT) {
        return cfd1_fail(stmt->dbh, stmt, "HYC00", "This statement has a forward-only cursor");
    }
    int64_t next;
    switch (orientation) {
        case PDO_FETCH_ORI_NEXT: next = (int64_t) data->current + 1; break;
        case PDO_FETCH_ORI_PRIOR: next = (int64_t) data->current - 1; break;
        case PDO_FETCH_ORI_FIRST: next = 0; break;
        case PDO_FETCH_ORI_LAST: next = (int64_t) data->rows - 1; break;
        case PDO_FETCH_ORI_ABS: next = offset; break;
        case PDO_FETCH_ORI_REL: next = (int64_t) data->current + offset; break;
        default: return cfd1_fail(stmt->dbh, stmt, "HY106", "Invalid cursor orientation");
    }
    if (next < 0) { data->current = -1; return 0; }
    if (next >= data->rows) { data->current = data->rows; return 0; }
    data->current = next;
    return 1;
}

static int cfd1_stmt_describe(pdo_stmt_t *stmt, int column)
{
    char *name = (char *) EM_ASM_PTR({
        const name = Module['__pdoCfd1'].statements.get($0).columns[$1];
        const size = lengthBytesUTF8(name) + 1;
        const pointer = _malloc(size);
        stringToUTF8(name, pointer, size);
        return pointer;
    }, stmt, column);
    stmt->columns[column].name = zend_string_init(name, strlen(name), 0);
    stmt->columns[column].maxlen = SIZE_MAX;
    stmt->columns[column].precision = 0;
#if PHP_VERSION_ID < 80100
    stmt->columns[column].param_type = PDO_PARAM_ZVAL;
#endif
    free(name);
    return 1;
}

/* Copy binary data and wide numbers without text decoding or 32-bit truncation. */
static int cfd1_read_value(pdo_stmt_t *stmt, zend_long row, int column, zval *value)
{
    int kind = EM_ASM_INT({
        const data = Module['__pdoCfd1'].statements.get($0);
        const value = data.rows[$1][data.columns[$2]];
        return value === null ? 0 : typeof value === 'number' ? 1 : typeof value === 'string' ? 2 : 3;
    }, stmt, row, column);
    if (!kind) { ZVAL_NULL(value); return 1; }
    if (kind == 1) {
        double number = EM_ASM_DOUBLE({
            const data = Module['__pdoCfd1'].statements.get($0);
            return data.rows[$1][data.columns[$2]];
        }, stmt, row, column);
        if (number >= ZEND_LONG_MIN && number <= ZEND_LONG_MAX && floor(number) == number) {
            ZVAL_LONG(value, (zend_long) number);
        } else ZVAL_DOUBLE(value, number);
        return 1;
    }
    size_t length = EM_ASM_INT({
        const data = Module['__pdoCfd1'].statements.get($0);
        const value = data.rows[$1][data.columns[$2]];
        return typeof value === 'string' ? lengthBytesUTF8(value) : value.length;
    }, stmt, row, column);
    zend_string *bytes = zend_string_alloc(length, 0);
    EM_ASM({
        const data = Module['__pdoCfd1'].statements.get($0);
        const value = data.rows[$1][data.columns[$2]];
        if (typeof value === 'string') stringToUTF8(value, $3, $4 + 1);
        else HEAPU8.set(value, $3);
    }, stmt, row, column, ZSTR_VAL(bytes), length);
    ZSTR_VAL(bytes)[length] = '\0';
    ZVAL_STR(value, bytes);
    return 1;
}

static int cfd1_get_value(pdo_stmt_t *stmt, int column, zval *value)
{
    cfd1_stmt *data = stmt->driver_data;
    if (column < 0 || column >= stmt->column_count || data->current < 0 || data->current >= data->rows) {
        ZVAL_NULL(value);
        return 0;
    }
    return cfd1_read_value(stmt, data->current, column, value);
}

#if PHP_VERSION_ID < 80100
static int cfd1_stmt_get_col(pdo_stmt_t *stmt, int column, char **pointer, size_t *length, int *caller_frees)
{
    zval *value = emalloc(sizeof(zval));
    ZVAL_NULL(value);
    int result = cfd1_get_value(stmt, column, value);
    *pointer = (char *) value;
    *length = sizeof(zval);
    *caller_frees = 1; /* PDO transfers the zval then frees its container. */
    return result;
}
#else
static int cfd1_stmt_get_col(pdo_stmt_t *stmt, int column, zval *value, enum pdo_param_type *type)
{
    return cfd1_get_value(stmt, column, value);
}
#endif

static int cfd1_stmt_col_meta(pdo_stmt_t *stmt, zend_long column, zval *result)
{
    cfd1_stmt *data = stmt->driver_data;
    if (!stmt->executed || column < 0 || column >= stmt->column_count || !data->rows) return FAILURE;
    zend_long row = data->current >= 0 && data->current < data->rows ? data->current : 0;
    zval value, flags;
    cfd1_read_value(stmt, row, column, &value);
    const char *native;
    enum pdo_param_type type;
    switch (Z_TYPE(value)) {
        case IS_NULL: native = "null"; type = PDO_PARAM_NULL; break;
        case IS_LONG: native = "integer"; type = PDO_PARAM_INT; break;
        case IS_DOUBLE: native = "double"; type = PDO_PARAM_STR; break;
        default:
            if (EM_ASM_INT({
                const data = Module['__pdoCfd1'].statements.get($0);
                return Array.isArray(data.rows[$1][data.columns[$2]]);
            }, stmt, row, column)) { native = "blob"; type = PDO_PARAM_LOB; }
            else { native = "string"; type = PDO_PARAM_STR; }
    }
    array_init(result);
    array_init(&flags);
    add_assoc_zval(result, "flags", &flags);
    add_assoc_string(result, "native_type", (char *) native);
    add_assoc_long(result, "pdo_type", type);
    zval_ptr_dtor(&value);
    return SUCCESS;
}

static int cfd1_stmt_get_attribute(pdo_stmt_t *stmt, zend_long attribute, zval *value)
{
    if (attribute == PDO_ATTR_CURSOR) {
        ZVAL_LONG(value, ((cfd1_stmt *)stmt->driver_data)->cursor);
        return 1;
    }
    if (attribute == PDO_ATTR_EMULATE_PREPARES) { ZVAL_FALSE(value); return 1; }
    return cfd1_fail(stmt->dbh, stmt, "HYC00", "Unsupported D1 statement attribute");
}

static int cfd1_stmt_close_cursor(pdo_stmt_t *stmt)
{
    cfd1_stmt_reset(stmt);
    return 1;
}

static const struct pdo_stmt_methods cfd1_stmt_methods = {
    .dtor = cfd1_stmt_destroy,
    .executer = cfd1_stmt_execute,
    .fetcher = cfd1_stmt_fetch,
    .describer = cfd1_stmt_describe,
    .get_col = cfd1_stmt_get_col,
    .get_attribute = cfd1_stmt_get_attribute,
    .get_column_meta = cfd1_stmt_col_meta,
    .cursor_closer = cfd1_stmt_close_cursor
};

static int cfd1_prepare_sql(pdo_dbh_t *dbh, const char *sql, size_t length, pdo_stmt_t *stmt, zval *options)
{
    cfd1_clear_error(dbh, NULL);
    zend_long cursor = pdo_attr_lval(options, PDO_ATTR_CURSOR, PDO_CURSOR_FWDONLY);
    if (cursor != PDO_CURSOR_FWDONLY && cursor != PDO_CURSOR_SCROLL) {
        return cfd1_fail(dbh, NULL, "HYC00", "Unsupported D1 cursor mode");
    }
    if (pdo_attr_lval(options, PDO_ATTR_EMULATE_PREPARES, 0)) {
        return cfd1_fail(dbh, NULL, "HYC00", "D1 uses native parameter binding");
    }
    if (memchr(sql, '\0', length)) return cfd1_fail(dbh, NULL, "HY000", "SQL must not contain NUL bytes");
    cfd1_stmt *data = ecalloc(1, sizeof(cfd1_stmt));
    data->current = -1;
    data->cursor = cursor;
    stmt->driver_data = data;
    stmt->methods = &cfd1_stmt_methods;
    stmt->supports_placeholders = PDO_PLACEHOLDER_POSITIONAL | PDO_PLACEHOLDER_NAMED;
    char *error = cfd1_js_prepare(stmt, dbh, sql, length);
    if (error) return cfd1_js_failure(dbh, NULL, error);
    return 1;
}

#if PHP_VERSION_ID < 80100
static int cfd1_prepare(pdo_dbh_t *dbh, const char *sql, size_t length, pdo_stmt_t *stmt, zval *options)
{
    return cfd1_prepare_sql(dbh, sql, length, stmt, options);
}
#else
static bool cfd1_prepare(pdo_dbh_t *dbh, zend_string *query, pdo_stmt_t *stmt, zval *options)
{
    return cfd1_prepare_sql(dbh, ZSTR_VAL(query), ZSTR_LEN(query), stmt, options);
}
#endif

#if PHP_VERSION_ID < 80100
static zend_long cfd1_doer(pdo_dbh_t *dbh, const char *sql, size_t length)
#else
static zend_long cfd1_doer(pdo_dbh_t *dbh, const zend_string *query)
#endif
{
#if PHP_VERSION_ID >= 80100
    const char *sql = ZSTR_VAL(query);
    size_t length = ZSTR_LEN(query);
#endif
    pdo_stmt_t statement = {0};
    statement.dbh = dbh;
    zend_long changes = -1;
    if (cfd1_prepare_sql(dbh, sql, length, &statement, NULL)) {
        if (cfd1_stmt_execute(&statement)) changes = statement.row_count;
        else cfd1_fail(dbh, NULL, statement.error_code, cfd1_error(dbh, &statement)->message);
    }
    php_pdo_stmt_set_column_count(&statement, 0);
    cfd1_stmt_destroy(&statement);
    return changes;
}

static zend_string *cfd1_quote_string(pdo_dbh_t *dbh, const char *value, size_t length, enum pdo_param_type type)
{
    cfd1_clear_error(dbh, NULL);
    if (length > (ZSTR_MAX_LEN - 3) / 2) {
        cfd1_fail(dbh, NULL, "22001", "Quoted value is too large");
        return NULL;
    }
    type = PDO_PARAM_TYPE(type);
    if (type != PDO_PARAM_LOB && memchr(value, '\0', length)) {
        cfd1_fail(dbh, NULL, "HY000", "Quoted text cannot contain NUL bytes; bind text or a BLOB instead");
        return NULL;
    }
    zend_string *quoted = zend_string_alloc(length * 2 + 3, 0);
    char *out = ZSTR_VAL(quoted);
    if (type == PDO_PARAM_LOB) {
        const char *hex = "0123456789abcdef";
        *out++ = 'X';
        *out++ = '\'';
        for (size_t i = 0; i < length; i++) {
            unsigned char byte = value[i];
            *out++ = hex[byte >> 4];
            *out++ = hex[byte & 15];
        }
    } else {
        *out++ = '\'';
        for (size_t i = 0; i < length; i++) {
            *out++ = value[i];
            if (value[i] == '\'') *out++ = '\'';
        }
    }
    *out++ = '\'';
    *out = '\0';
    ZSTR_LEN(quoted) = out - ZSTR_VAL(quoted);
    return quoted;
}

#if PHP_VERSION_ID < 80100
static int cfd1_quoter(pdo_dbh_t *dbh, const char *value, size_t length, char **quoted, size_t *quoted_length, enum pdo_param_type type)
{
    zend_string *result = cfd1_quote_string(dbh, value, length, type);
    if (!result) return 0;
    *quoted_length = ZSTR_LEN(result);
    *quoted = estrndup(ZSTR_VAL(result), ZSTR_LEN(result));
    zend_string_release(result);
    return 1;
}
#else
static zend_string *cfd1_quoter(pdo_dbh_t *dbh, const zend_string *value, enum pdo_param_type type)
{
    return cfd1_quote_string(dbh, ZSTR_VAL(value), ZSTR_LEN(value), type);
}
#endif

static zend_string *cfd1_last_id_string(pdo_dbh_t *dbh, bool named)
{
    cfd1_clear_error(dbh, NULL);
    if (named) {
        cfd1_fail(dbh, NULL, "HYC00", "D1 does not support named sequences");
        return NULL;
    }
    char *value = (char *) EM_ASM_PTR({
        const id = Module['__pdoCfd1'].connections.get($0).lastId;
        if (id === null) return 0;
        const length = lengthBytesUTF8(id) + 1;
        const pointer = _malloc(length);
        stringToUTF8(id, pointer, length);
        return pointer;
    }, dbh);
    if (!value) {
        cfd1_fail(dbh, NULL, "22003", "D1 did not provide an exact, safe insert ID");
        return NULL;
    }
    zend_string *result = zend_string_init(value, strlen(value), 0);
    free(value);
    return result;
}

#if PHP_VERSION_ID < 80100
static char *cfd1_last_id(pdo_dbh_t *dbh, const char *name, size_t *length)
{
    zend_string *id = cfd1_last_id_string(dbh, name != NULL);
    if (!id) return NULL;
    *length = ZSTR_LEN(id);
    char *result = estrndup(ZSTR_VAL(id), ZSTR_LEN(id));
    zend_string_release(id);
    return result;
}
#else
static zend_string *cfd1_last_id(pdo_dbh_t *dbh, const zend_string *name)
{
    return cfd1_last_id_string(dbh, name != NULL);
}
#endif

static CFD1_BOOL cfd1_set_attribute(pdo_dbh_t *dbh, zend_long attribute, zval *value)
{
    cfd1_clear_error(dbh, NULL);
    if (attribute == PDO_ATTR_AUTOCOMMIT && zend_is_true(value)) return true;
    if (attribute == PDO_ATTR_EMULATE_PREPARES && !zend_is_true(value)) return true;
    return cfd1_fail(dbh, NULL, "HYC00", "Unsupported D1 attribute setting");
}

static int cfd1_get_attribute(pdo_dbh_t *dbh, zend_long attribute, zval *value)
{
    cfd1_clear_error(dbh, NULL);
    switch (attribute) {
        case PDO_ATTR_AUTOCOMMIT: ZVAL_TRUE(value); return 1;
        case PDO_ATTR_EMULATE_PREPARES: ZVAL_FALSE(value); return 1;
        case PDO_ATTR_CLIENT_VERSION: ZVAL_STRING(value, PHP_PDO_CFD1_VERSION); return 1;
        case PDO_ATTR_SERVER_VERSION:
            cfd1_fail(dbh, NULL, "HYC00", "D1 does not expose its SQLite server version");
            return -1;
        default: cfd1_fail(dbh, NULL, "HYC00", "Unsupported D1 attribute"); return -1;
    }
}

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_cfd1_batch, 0, 1, _IS_BOOL, 0)
    ZEND_ARG_TYPE_INFO(0, statements, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

PHP_METHOD(PDO, cfd1Batch)
{
    zval *input, *entry;
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ARRAY(input)
    ZEND_PARSE_PARAMETERS_END();
    pdo_dbh_t *dbh = Z_PDO_DBH_P(ZEND_THIS);
    if (!dbh->driver || !dbh->driver_data || !dbh->methods || dbh->methods->preparer != cfd1_prepare) {
        zend_throw_exception_ex(php_pdo_get_exception(), 0, "cfd1Batch requires a cfd1 PDO connection");
        RETURN_FALSE;
    }
    cfd1_clear_error(dbh, NULL);
    uint32_t count = zend_hash_num_elements(Z_ARRVAL_P(input)), used = 0;
    if (!count) RETURN_TRUE;
    pdo_stmt_t **statements = safe_emalloc(count, sizeof(pdo_stmt_t *), 0);
    zend_string *class_name = zend_string_init("PDOStatement", sizeof("PDOStatement") - 1, 0);
    zend_class_entry *statement_class = zend_lookup_class(class_name);
    zend_string_release(class_name);
    HashTable seen;
    zend_hash_init(&seen, count, NULL, NULL, 0);
    zend_ulong index;
    zend_string *key;
    bool valid = true;
    ZEND_HASH_FOREACH_KEY_VAL(Z_ARRVAL_P(input), index, key, entry) {
        ZVAL_DEREF(entry);
        if (key || index != used || Z_TYPE_P(entry) != IS_OBJECT || !instanceof_function(Z_OBJCE_P(entry), statement_class)) {
            cfd1_fail(dbh, NULL, "HY105", "cfd1Batch expects a list of PDO statements");
            valid = false;
            break;
        }
        pdo_stmt_t *stmt = Z_PDO_STMT_P(entry);
        if (stmt->dbh != dbh || stmt->methods != &cfd1_stmt_methods || !stmt->driver_data) {
            cfd1_fail(dbh, NULL, "HY000", "Every batch statement must belong to this PDO connection");
            valid = false;
            break;
        }
        if (!zend_hash_index_add_empty_element(&seen, (zend_ulong)(uintptr_t) stmt)) {
            cfd1_fail(dbh, NULL, "HY000", "Each batch entry must be a distinct PDO statement");
            valid = false;
            break;
        }
        /* User stream callbacks can replace referenced entries in the input. */
        GC_ADDREF(&stmt->std);
        statements[used++] = stmt;
    } ZEND_HASH_FOREACH_END();
    zend_hash_destroy(&seen);
    if (valid) {
        for (uint32_t i = 0; i < count; i++) cfd1_stmt_reset(statements[i]);
        for (uint32_t i = 0; i < count; i++) {
            if (!cfd1_collect_parameters(statements[i])) {
                cfd1_fail(dbh, NULL, statements[i]->error_code, cfd1_error(dbh, statements[i])->message);
                valid = false;
                break;
            }
        }
        if (valid) {
            char *error = cfd1_js_batch(dbh, (void **) statements, count);
            if (error) { cfd1_js_failure(dbh, NULL, error); valid = false; }
        }
        for (uint32_t i = 0; i < count; i++) {
            if (valid) cfd1_stmt_publish(statements[i]);
            else {
                cfd1_stmt_reset(statements[i]);
                cfd1_fail(dbh, statements[i], dbh->error_code, cfd1_error(dbh, NULL)->message);
            }
        }
    }
    if (!valid) cfd1_report_error(dbh);
    for (uint32_t i = 0; i < used; i++) OBJ_RELEASE(&statements[i]->std);
    efree(statements);
    RETURN_BOOL(valid);
}

static const zend_function_entry cfd1_methods[] = {
    PHP_ME(PDO, cfd1Batch, arginfo_cfd1_batch, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

static CFD1_VOID cfd1_close(pdo_dbh_t *dbh)
{
    cfd1_db *data = dbh->driver_data;
    if (data) {
        EM_ASM({ Module['__pdoCfd1'].connections.delete($0); }, dbh);
        if (data->error.message) pefree(data->error.message, dbh->is_persistent);
        pefree(data, dbh->is_persistent);
        dbh->driver_data = NULL;
    }
    CFD1_RETURN_VOID;
}

static const struct pdo_dbh_methods cfd1_db_methods = {
    .closer = cfd1_close,
    .preparer = cfd1_prepare,
    .doer = cfd1_doer,
    .quoter = cfd1_quoter,
    .set_attribute = cfd1_set_attribute,
    .last_id = cfd1_last_id,
    .fetch_err = cfd1_fetch_error,
    .get_attribute = cfd1_get_attribute
};

static int cfd1_connect(pdo_dbh_t *dbh, zval *options)
{
    dbh->driver_data = pecalloc(1, sizeof(cfd1_db), dbh->is_persistent);
    dbh->methods = &cfd1_db_methods;
    cfd1_clear_error(dbh, NULL);
    if (dbh->is_persistent) {
        cfd1_fail(dbh, NULL, "HYC00", "Persistent D1 connections are not supported");
    } else if (!pdo_attr_lval(options, PDO_ATTR_AUTOCOMMIT, 1) || pdo_attr_lval(options, PDO_ATTR_EMULATE_PREPARES, 0)) {
        cfd1_fail(dbh, NULL, "HYC00", "D1 requires autocommit and native parameter binding");
    } else {
        char *error = cfd1_js_connect(dbh, dbh->data_source);
        if (!error) return 1;
        cfd1_js_failure(dbh, NULL, error);
    }
    pdo_throw_exception(1, cfd1_error(dbh, NULL)->message, &dbh->error_code);
    return 0;
}

static const pdo_driver_t cfd1_driver = { PDO_DRIVER_HEADER(cfd1), cfd1_connect };

PHP_MINIT_FUNCTION(pdo_cfd1)
{
    cfd1_js_init();
    /* PHP 8.5 unconditionally deprecates get_driver_methods callbacks.
     * Register a real, driver-prefixed PDO method so the same call works on
     * 8.0-8.5 without replacing the user's PDO constructor or suppressing errors. */
    zend_class_entry *pdo = php_pdo_get_dbh_ce();
    if (zend_register_functions(pdo, cfd1_methods, &pdo->function_table, MODULE_PERSISTENT) == FAILURE) return FAILURE;
    return php_pdo_register_driver(&cfd1_driver);
}

PHP_MSHUTDOWN_FUNCTION(pdo_cfd1)
{
    php_pdo_unregister_driver(&cfd1_driver);
    EM_ASM({ delete Module['__pdoCfd1']; });
    return SUCCESS;
}

PHP_MINFO_FUNCTION(pdo_cfd1)
{
    php_info_print_table_start();
    php_info_print_table_row(2, "Cloudflare D1 PDO support", "enabled (native parameters and atomic batches)");
    php_info_print_table_end();
}

zend_module_entry pdo_cfd1_module_entry = {
    STANDARD_MODULE_HEADER, "pdo_cfd1", NULL,
    PHP_MINIT(pdo_cfd1), PHP_MSHUTDOWN(pdo_cfd1), NULL, NULL,
    PHP_MINFO(pdo_cfd1), PHP_PDO_CFD1_VERSION, STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_PDO_CFD1
# ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
# endif
ZEND_GET_MODULE(pdo_cfd1)
#endif
