dnl config.m4 for extension php-pdo-cfd1

PHP_ARG_ENABLE([php-pdo-cfd1],
  [whether to enable pdo_cfd1 support],
  [AS_HELP_STRING([--enable-pdo-cfd1],
    [Enable pdo_cfd1 support])],
  [no])

if test "$PHP_PDO_CFD1" != "no"; then
  PHP_NEW_EXTENSION(pdo_cfd1, pdo_cfd1.c, $ext_shared)
fi
