# pdo-cfd1

*pdo driver for CloudFlare D1 & php-wasm*

[![Static Badge](https://img.shields.io/badge/reddit-always%20online-336699?style=for-the-badge&logo=reddit)](https://www.reddit.com/r/phpwasm/) [![Discord](https://img.shields.io/discord/1199824765666463835?style=for-the-badge&logo=discord&link=https%3A%2F%2Fdiscord.gg%2Fj8VZzju7gJ)](https://discord.gg/j8VZzju7gJ)

### Join the community: [reddit](https://www.reddit.com/r/phpwasm/) | [discord](https://discord.gg/j8VZzju7gJ) | [php-wasm](https://github.com/seanmorris/php-wasm)

### Coming Soon! Tutorial on getting php-wasm running in CloudFlare.

## Connect & Configure

Simply pass the D1 object into the php-wasm constructor as `cfd1` to enable pdo_cfd1 support. Once D1 is passed in, `cfd1:` will be available as a PDO driver.

```javascript
export async function onRequest(event)
{
    const mainDb = event.env.mainDb;
    const php = new PhpWorker({ cfd1: { mainDb } });
    
    php.run(`<?php $pdo = new PDO('cfd1:mainDb');`);
}
```

You can check `phpinfo()` to make sure that the D1 object is detected. `CloudFlare D1 SQL module detected` will display "yes" when the object has been passed in correctly:

![](https://raw.githubusercontent.com/seanmorris/pdo-cfd1/refs/heads/master/phpinfo.png)

PDO can be used with D1 just like any other SQL server:

```javascript
export async function onRequest(event)
{
    const mainDb = event.env.mainDb;
    const php = new PhpWorker({ cfd1: { mainDb } });
    
    php.run(`<?php
        $pdo = new PDO('cfd1:main');
        $select = $pdo->prepare('SELECT PageTitle, PageContent FROM WikiPages WHERE PageTitle = ?');
        $select->execute([$pageTitle]);
        $page = $select->fetchObject();`
    );
}
```

## Todo

* *Named replacement tokens* - Currently only positional tokens are supported.
* *Error handling* - Error handling is currently very rudimentary and does not propagage messages.

## CloudFlare D1

`pdo_cfd1` is powered by [CloudFlare D1](https://developers.cloudflare.com/d1/).

https://developers.cloudflare.com/d1/
