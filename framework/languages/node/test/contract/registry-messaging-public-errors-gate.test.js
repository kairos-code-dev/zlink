const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const root = path.resolve(__dirname, '../..');

function source(relativePath) {
  return fs.readFileSync(path.join(root, relativePath), 'utf8');
}

test('RM-C5 and RM-C8 classify failures by public error kind', () => {
  const endpoints = source('e2e/RegistryMessaging/Server/Consumer/Endpoints/consumer-endpoints.ts');
  const missing = source('e2e/RegistryMessaging/Client/Scenarios/rm-c5-missing-packet-scenario.ts');
  const payload = source('e2e/RegistryMessaging/Client/Scenarios/rm-c8-payload-round-trip-scenario.ts');

  assert.match(endpoints, /error instanceof ZLinkFrameworkException \? error\.kind/);
  assert.match(missing, /ZLinkFrameworkErrorKind\.HandlerNotFound/);
  assert.match(missing, /reason=handlerMissing.*action=replyError/s);
  assert.match(missing, /reason=handlerMissing.*action=drop/s);
  assert.match(payload, /ZLinkFrameworkErrorKind\.RequestFailed/);
});
