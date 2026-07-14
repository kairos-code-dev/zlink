const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const root = path.resolve(__dirname, '../..');

test('TA-A1 through TA-A3 assert bound-session snapshots', () => {
  const client = fs.readFileSync(path.join(root, 'e2e/ToActorMessaging/Client/main.ts'), 'utf8');
  const session = fs.readFileSync(path.join(root, 'e2e/ToActorMessaging/Server/Session/main.ts'), 'utf8');
  const runner = fs.readFileSync(path.join(root, 'e2e/ToActorMessaging/run_e2e.sh'), 'utf8');

  assert.match(session, /path: '\/bindings\/snapshot'/);
  assert.match(session, /context\.sessionId/);
  assert.match(client, /assertSameBinding\(a1Before, a1After/);
  assert.match(client, /assertUnbound\(a2Before/);
  assert.match(client, /assertUnbound\(a2After/);
  assert.match(client, /assertUnbound\(a3Before/);
  assert.match(client, /assertBound\(a3After/);
  assert.match(runner, /--session-url "\$SESSION_URL"/);
});
