const assert = require('node:assert/strict');
const test = require('node:test');

const {
  ZLinkDeferredJoinAcceptedJournal
} = require('../../packages/framework/dist/runtime/actors/deferred-join-accepted-journal');
const {
  ZLinkActorDispatchMailbox
} = require('../../packages/framework/dist/runtime/actors/actor-mailbox');
const {
  encodeActorAuthorityIdentity
} = require('../../packages/framework/dist/runtime/actors/actor-authority-publication');

function harness() {
  const roots = new Map();
  const events = [];
  let rootSequence = 0;
  let authorityVersion = 1;
  let authority = {
    kind: 'snapshot',
    storeVersion: { value: String(authorityVersion) },
    payload: encodeActorAuthorityIdentity({
      actorType: 'player',
      actor: {
        nodeRid: 'node-a',
        actorId: 'actor-a',
        generation: 17n
      },
      meshName: 'game',
      ownerNodeGeneration: 1n,
      owner: {
        ownerId: 'owner-a',
        leaseGeneration: 5n
      }
    }),
    objectGeneration: 17n,
    authorityOwnerGeneration: 3n,
    ownerId: 'owner-a',
    ownerLeaseGeneration: 5n,
    allocation: {
      state: 'active',
      objectKind: 'actor',
      stableType: 'player',
      descriptor: { meshName: 'game', rid: 'node-a' },
      descriptorLifecycleGeneration: 1n,
      capacity: { actors: 1, spots: 0 }
    },
    storeNow: new Date(100)
  };
  const authorityStore = {
    async readAuthority() {
      return authority;
    },
    async compareExchangeAuthority(_key, expected, mutation) {
      events.push('authority-cas');
      if (expected.value !== authority.storeVersion.value) {
        return { kind: 'conflict', current: authority };
      }
      authorityVersion++;
      authority = {
        ...authority,
        storeVersion: { value: String(authorityVersion) },
        payload: Buffer.from(mutation.payload)
      };
      const { kind: _kind, ...stored } = authority;
      return { kind: 'stored', ...stored };
    }
  };
  const relocationStore = {
    async putRelocation(payload) {
      const reference = { value: `root-${++rootSequence}` };
      const stable = Buffer.from(payload);
      roots.set(reference.value, stable);
      events.push(`put:${reference.value}`);
      return {
        reference,
        checksumCrc32c: crc32c(stable),
        expiresAt: new Date(86_400_100),
        storeNow: new Date(100)
      };
    },
    async getRelocation(reference) {
      const payload = roots.get(reference.value);
      return payload === undefined
        ? { kind: 'missing' }
        : { kind: 'found', payload: Buffer.from(payload) };
    },
    async deleteRelocation(reference) {
      events.push(`delete:${reference.value}`);
      return roots.delete(reference.value) ? 'deleted' : 'missing';
    }
  };
  return {
    events,
    roots,
    journal: new ZLinkDeferredJoinAcceptedJournal(authorityStore, relocationStore)
  };
}

test('cross-node Accepted root preserves identity, reply and cursor across mailbox retry', async () => {
  const { events, roots, journal } = harness();
  const sourceActorRef = {
    nodeRid: 'node-a',
    actorId: 'actor-a',
    generation: 17n
  };
  const actorRef = {
    nodeRid: 'node-b',
    actorId: 'actor-a',
    generation: 17n
  };
  const operationId = { high: 41n, low: 73n };
  let root = await journal.prepare(
    sourceActorRef.actorId,
    operationId,
    sourceActorRef,
    Buffer.from('"accepted"')
  );
  assert.equal(root.cursor, 'prepared');
  assert.equal(root.reference.value, 'root-1');

  root = await journal.markCommitted(root, actorRef);
  assert.equal(root.cursor, 'committed');
  assert.equal(root.reference.value, 'root-2');
  assert.equal(root.actor.nodeRid, 'node-b');
  assert.equal(roots.has('root-1'), false);

  const mailbox = new ZLinkActorDispatchMailbox();
  const callbackEvents = [];
  let attempts = 0;
  const actor = {
    actorId: actorRef.actorId,
    async onJoinCompleted(completion) {
      callbackEvents.push(
        `completion:${completion.operationId.high}:${completion.actor.nodeRid}:${completion.actor.generation}`
      );
      attempts++;
      if (attempts === 1) throw new Error('retry');
    }
  };
  const backlog = mailbox.submit(async () => {
    callbackEvents.push('backlog');
  });
  await assert.rejects(
    journal.deliver(root, actor, actorRef, operation => mailbox.submit(operation)),
    /retry/
  );
  await backlog;
  assert.deepEqual(callbackEvents, [
    'backlog',
    'completion:41:node-b:17'
  ]);
  assert.equal((await journal.recover(actorRef.actorId)).cursor, 'committed');

  root = await journal.deliver(
    root,
    actor,
    actorRef,
    operation => mailbox.submit(operation)
  );
  assert.equal(root.cursor, 'delivered');
  assert.equal(root.reference.value, 'root-3');
  assert.equal(roots.has('root-2'), false);

  await journal.deliver(
    root,
    actor,
    actorRef,
    operation => mailbox.submit(operation)
  );
  assert.equal(attempts, 2);
  assert.deepEqual(events, [
    'put:root-1',
    'authority-cas',
    'put:root-2',
    'authority-cas',
    'delete:root-1',
    'put:root-3',
    'authority-cas',
    'delete:root-2'
  ]);
});

test('generation fence rejects a replacement Actor before mailbox admission', async () => {
  const { journal } = harness();
  const sourceActorRef = {
    nodeRid: 'node-a',
    actorId: 'actor-a',
    generation: 17n
  };
  const actorRef = {
    nodeRid: 'node-b',
    actorId: 'actor-a',
    generation: 17n
  };
  const root = await journal.markCommitted(await journal.prepare(
    actorRef.actorId,
    { high: 1n, low: 2n },
    sourceActorRef,
    Buffer.alloc(0)
  ), actorRef);
  let admitted = false;
  await assert.rejects(
    journal.deliver(
      root,
      { actorId: actorRef.actorId },
      { ...actorRef, generation: 18n },
      async operation => {
        admitted = true;
        return await operation();
      }
    ),
    /generation fence/
  );
  assert.equal(admitted, false);
});

function crc32c(payload) {
  let crc = 0xffff_ffff;
  for (const byte of payload) {
    crc ^= byte;
    for (let bit = 0; bit < 8; bit++) {
      crc = (crc >>> 1) ^ ((crc & 1) === 0 ? 0 : 0x82f6_3b78);
    }
  }
  return (crc ^ 0xffff_ffff) >>> 0;
}
