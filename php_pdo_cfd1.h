#ifndef PHP_PDO_CFD1_H
#define PHP_PDO_CFD1_H
extern zend_module_entry pdo_cfd1_module_entry;
#define phpext_pdo_cfd1_ptr &pdo_cfd1_module_entry
#define PHP_PDO_CFD1_VERSION "0.1.0-php-wasm"
#if defined(ZTS) && defined(COMPILE_DL_PDO_CFD1)
ZEND_TSRMLS_CACHE_EXTERN()
#endif
#endif
