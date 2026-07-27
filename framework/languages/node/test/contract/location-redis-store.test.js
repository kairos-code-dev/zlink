const assert = require('node:assert/strict');
const test = require('node:test');
const { createClient } = require('redis');
const redisLocations = require('../../packages/framework-locations-redis/dist');

test('redis provider exports only the two opaque Store implementations', () => {
  assert.deepEqual(
    Object.keys(redisLocations).sort(),
    ['ZLinkRedisLocationStore', 'ZLinkRedisRelocationStore']
  );
  const locationMethods = Object.getOwnPropertyNames(
    redisLocations.ZLinkRedisLocationStore.prototype
  ).sort();
  for (const method of ['dispose', 'read', 'scan', 'write']) {
    assert.equal(locationMethods.includes(method), true);
  }
  for (const removed of ['claimOwnerLease', 'resolveSpot', 'reserve', 'prepareAggregate']) {
    assert.equal(locationMethods.includes(removed), false);
  }
  const relocationMethods = Object.getOwnPropertyNames(
    redisLocations.ZLinkRedisRelocationStore.prototype
  ).sort();
  for (const method of ['delete', 'dispose', 'put', 'read', 'renew']) {
    assert.equal(relocationMethods.includes(method), true);
  }
  for (const removed of ['putRelocation', 'getRelocation', 'renewRelocation']) {
    assert.equal(relocationMethods.includes(removed), false);
  }
});

test('redis opaque Location Store applies conditional batches atomically', async (t) => {
  const fixture = await redisFixture(t);
  if (fixture === undefined) return;
  const prefix = testPrefix('location-batch');
  const store = new redisLocations.ZLinkRedisLocationStore({
    url: fixture.url,
    keyPrefix: prefix
  });
  const alpha = key('object/alpha');
  const beta = key('object/beta');

  try {
    const missing = await store.read(alpha);
    assert.equal(missing.kind, 'missing');
    assert.ok(missing.storeNow instanceof Date);

    const first = await store.write({
      conditions: [{ kind: 'missing', key: alpha }, { kind: 'missing', key: beta }],
      mutations: [
        { kind: 'put', key: alpha, bytes: Uint8Array.from([0, 1, 255]) },
        { kind: 'put', key: beta, bytes: Buffer.from('before') }
      ]
    });
    assert.equal(first.kind, 'applied');
    assert.equal(first.putVersions.length, 2);
    const alphaVersion = first.putVersions.find(item => item.key.value === alpha.value).version;

    const conflict = await store.write({
      conditions: [
        { kind: 'version', key: alpha, expected: version('stale') },
        { kind: 'version', key: beta, expected: first.putVersions[1].version }
      ],
      mutations: [
        { kind: 'put', key: alpha, bytes: Buffer.from('changed') },
        { kind: 'delete', key: beta }
      ]
    });
    assert.equal(conflict.kind, 'conflict');
    assert.deepEqual([...((await store.read(alpha)).value.bytes)], [0, 1, 255]);
    assert.equal(Buffer.from((await store.read(beta)).value.bytes).toString(), 'before');

    const updated = await store.write({
      conditions: [{ kind: 'version', key: alpha, expected: alphaVersion }],
      mutations: [{ kind: 'put', key: alpha, bytes: Buffer.from('after') }]
    });
    assert.equal(updated.kind, 'applied');
    const read = await store.read(alpha);
    assert.equal(read.kind, 'found');
    assert.equal(Buffer.from(read.value.bytes).toString(), 'after');
    assert.notEqual(read.value.version.value, alphaVersion.value);
    assert.equal(read.value.storeNow.getTime() <= Date.now(), true);
  } finally {
    await store.dispose();
    await cleanup(fixture.client, prefix);
    await fixture.client.quit();
  }
});

test('redis opaque Location Store enforces TTL and fixed scan snapshots', async (t) => {
  const fixture = await redisFixture(t);
  if (fixture === undefined) return;
  const prefix = testPrefix('location-scan');
  const store = new redisLocations.ZLinkRedisLocationStore({
    url: fixture.url,
    keyPrefix: prefix
  });

  try {
    const expiring = key('ttl/item');
    await store.write({
      conditions: [{ kind: 'missing', key: expiring }],
      mutations: [{ kind: 'put', key: expiring, bytes: Buffer.from('ttl'), retentionMs: 20 }]
    });
    await new Promise(resolve => setTimeout(resolve, 40));
    assert.equal((await store.read(expiring)).kind, 'missing');

    await store.write({
      conditions: [],
      mutations: [
        { kind: 'put', key: key('scan/a'), bytes: Buffer.from('A') },
        { kind: 'put', key: key('scan/b'), bytes: Buffer.from('B') }
      ]
    });
    const first = await store.scan({ prefix: 'scan/', limit: 1 });
    assert.equal(first.kind, 'page');
    assert.equal(first.value.items.length, 1);
    assert.ok(first.value.nextCursor);

    await store.write({
      conditions: [],
      mutations: [
        { kind: 'delete', key: key('scan/b') },
        { kind: 'put', key: key('scan/c'), bytes: Buffer.from('C') }
      ]
    });
    const second = await store.scan({
      prefix: 'scan/',
      cursor: first.value.nextCursor,
      limit: 10
    });
    assert.equal(second.kind, 'page');
    assert.deepEqual(
      [...first.value.items, ...second.value.items].map(item => item.key.value),
      ['scan/a', 'scan/b']
    );

    const expired = await store.scan({
      prefix: 'scan/',
      cursor: cursor('00000000-0000-0000-0000-000000000000:0'),
      limit: 10
    });
    assert.deepEqual(expired, { kind: 'expired' });
  } finally {
    await store.dispose();
    await cleanup(fixture.client, prefix);
    await fixture.client.quit();
  }
});

test('redis opaque Relocation Store uses Framework-issued immutable references', async (t) => {
  const fixture = await redisFixture(t);
  if (fixture === undefined) return;
  const prefix = testPrefix('relocation');
  const store = new redisLocations.ZLinkRedisRelocationStore({
    url: fixture.url,
    keyPrefix: prefix
  });
  const blob = reference('framework-issued-reference');
  const payload = Uint8Array.from([0, 1, 2, 255]);

  try {
    const stored = await store.put(blob, payload, 60_000);
    assert.equal(stored.kind, 'stored');
    assert.equal(stored.expiresAt > stored.storeNow, true);
    const duplicate = await store.put(blob, payload, 60_000);
    assert.equal(duplicate.kind, 'alreadyStored');
    const conflict = await store.put(blob, Buffer.from('different'), 60_000);
    assert.equal(conflict.kind, 'conflict');

    const found = await store.read(blob);
    assert.equal(found.kind, 'found');
    assert.deepEqual([...found.bytes], [...payload]);
    const renewed = await store.renew(blob, 120_000);
    assert.equal(renewed.kind, 'renewed');
    assert.equal(renewed.expiresAt > renewed.storeNow, true);

    await store.delete(blob);
    await store.delete(blob);
    assert.equal((await store.read(blob)).kind, 'missing');
    assert.equal((await store.renew(blob, 10_000)).kind, 'missing');
  } finally {
    await store.dispose();
    await cleanup(fixture.client, prefix);
    await fixture.client.quit();
  }
});

test('redis Store validates exact public bounds', async () => {
  assert.throws(
    () => new redisLocations.ZLinkRedisLocationStore({
      url: 'redis://127.0.0.1:6379',
      keyPrefix: ''
    }),
    /keyPrefix/
  );
  const location = new redisLocations.ZLinkRedisLocationStore({
    url: 'redis://127.0.0.1:6379',
    keyPrefix: 'bounds'
  });
  await assert.rejects(
    location.write({
      conditions: [],
      mutations: [{ kind: 'put', key: key('large'), bytes: new Uint8Array(1024 * 1024 + 1) }]
    }),
    /1 MiB/
  );
  await assert.rejects(location.scan({ prefix: '', limit: 0 }), /1..1000/);
  await location.dispose();
});

async function redisFixture(t) {
  const candidates = [
    process.env.ZLINK_REDIS_TEST_ENDPOINT,
    '127.0.0.1:16379',
    '127.0.0.1:6379'
  ].filter(Boolean);
  for (const endpoint of candidates) {
    const url = endpoint.startsWith('redis://') ? endpoint : `redis://${endpoint}`;
    const client = createClient({
      url,
      socket: { connectTimeout: 300, reconnectStrategy: false }
    });
    client.on('error', () => {});
    try {
      await Promise.race([
        client.connect(),
        new Promise((_, reject) =>
          setTimeout(() => reject(new Error('Redis probe timeout')), 500)
        )
      ]);
      await client.ping();
      return { url, client };
    } catch {
      try {
        if (client.isOpen) await client.disconnect();
      } catch {}
    }
  }
  t.skip('Redis is not reachable.');
  return undefined;
}

async function cleanup(client, prefix) {
  let cursorValue = '0';
  do {
    const page = await client.scan(cursorValue, {
      MATCH: `${prefix}:*`,
      COUNT: 100
    });
    cursorValue = String(page.cursor);
    if (page.keys.length > 0) await client.del(page.keys);
  } while (cursorValue !== '0');
}

function testPrefix(scope) {
  return `zlink:opaque:${scope}:${process.pid}:${Date.now()}`;
}

function key(value) {
  return { value };
}

function version(value) {
  return { value };
}

function cursor(value) {
  return { value };
}

function reference(value) {
  return { value };
}
