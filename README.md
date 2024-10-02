# pdo-cfd1

*pdo driver for CloudFlare D1 & php-wasm*

[![Static Badge](https://img.shields.io/badge/reddit-always%20online-336699?style=for-the-badge&logo=reddit)](https://www.reddit.com/r/phpwasm/) [![Discord](https://img.shields.io/discord/1199824765666463835?style=for-the-badge&logo=discord&link=https%3A%2F%2Fdiscord.gg%2Fj8VZzju7gJ)](https://discord.gg/j8VZzju7gJ)

### Join the community: [reddit](https://www.reddit.com/r/phpwasm/) | [discord](https://discord.gg/j8VZzju7gJ) | [php-wasm](https://github.com/seanmorris/php-wasm)

## Connect & Configure

Simply pass the D1 object into the php-wasm constructor as `cfdb` to enable pdo_cfd1 support:

```javascript
const php = new PhpWorker({ cfdb: context.env.db });
```

Once D1 is passed in, `cfd1:` will be available as a PDO driver.

```javascript
const php = new PhpWorker({ cfdb: context.env.db });

php.run(`<?php $pdo = new PDO('cfd1:');`);
```

You can check `phpinfo()` to make sure that the D1 object is detected. `CloudFlare D1 SQL module detected` will display "yes" when the object has been passed in correctly:

![](https://raw.githubusercontent.com/seanmorris/pdo-cfd1/refs/heads/master/phpinfo.png)

PDO can be used with D1 just like any other SQL server:

```php
const php = new PhpWorker({ cfdb: context.env.db });

php.run(`<?php
	$pdo = new PDO('cfd1:');
	$select = $pdo->prepare('SELECT PageTitle, PageContent FROM WikiPages WHERE PageTitle = ?');
	$select->execute([$pageTitle]);
	$page = $select->fetchObject();
`);
```

## CloudFlare D1

`pdo_cfd1` is powered by [CloudFlare D1](https://developers.cloudflare.com/d1/).

https://developers.cloudflare.com/d1/



