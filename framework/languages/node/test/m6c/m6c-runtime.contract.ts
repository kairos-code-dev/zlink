import assert from 'node:assert/strict';
import { test } from 'node:test';
import {
  ServiceMaintenanceRuntime,
  classifyRelocationRecovery
} from '../../packages/framework/src/runtime/foundation/service-maintenance-runtime';
import {
  ServiceMailbox
} from '../../packages/framework/src/runtime/foundation/service-mailbox';
import {
  InMemoryServiceLocationAuthority
} from '../../packages/framework/src/runtime/foundation/service-location-authority';
import {
  ServiceDurableRelocationRuntime,
  ServiceRelocationDataLostError,
  crc32c,
  decodeServiceRelocationEnvelope,
  encodeServiceRelocationEnvelope,
  type ServiceRelocationAuthorityCodec,
  type ServiceRelocationEnvelope,
  type ServiceRelocationPublication,
  type ServiceRelocationStorePort
} from '../../packages/framework/src/runtime/foundation/service-relocation-runtime';
import {
  ZLinkManagedTimer
} from '../../packages/framework/src/runtime/spots/spot-timer';
import {
  ZLinkTimerOverrunPolicy
} from '../../packages/framework/src/contracts';

test('Retire preflight precedes publication and ready units use bounded permits', async () => {
  const events: string[] = [];
  let active = 0;
  let peak = 0;
  const runtime = new ServiceMaintenanceRuntime({
    maxOutbound: 2,
    maxInFlightBytes: 20,
    preflight: async () => {
      events.push('preflight');
      return true;
    },
    publishState: state => events.push(state),
    forceStop: () => {
      events.push('force');
    }
  });
  for (let index = 0; index < 4; index++) {
    runtime.enqueue({
      id: `unit-${index}`,
      encodedUpperBound: 10,
      ready: () => true,
      relocate: async () => {
        active++;
        peak = Math.max(peak, active);
        await new Promise(resolve => setImmediate(resolve));
        active--;
      }
    });
  }
  const terminal = await runtime.start('retire', 2_000);
  assert.equal(terminal.state, 'completed');
  assert.equal(peak, 2);
  assert.deepEqual(events.slice(0, 3), ['preflight', 'retiring', 'draining']);
});

test('first maintenance intent wins and blocked preflight keeps admission uncommitted', async () => {
  const runtime = new ServiceMaintenanceRuntime({
    preflight: async () => false,
    publishState: () => assert.fail('blocked preflight must not publish host state'),
    forceStop: () => assert.fail('preflight failure must not force stop')
  });
  const first = runtime.start('retire', 100);
  const second = runtime.start('shutdown', 100);
  assert.equal(first, second);
  const terminal = await first;
  assert.equal(terminal.kind, 'retire');
  assert.equal(terminal.state, 'blocked');
});

test('deadline after publication forces bounded terminal shutdown and observers see it', async () => {
  const states: string[] = [];
  let forced = 0;
  const runtime = new ServiceMaintenanceRuntime({
    preflight: async () => true,
    publishState: () => {},
    forceStop: () => {
      forced++;
    }
  });
  runtime.observe(snapshot => states.push(snapshot.state));
  runtime.enqueue({
    id: 'slow',
    encodedUpperBound: 1,
    ready: () => true,
    relocate: signal => new Promise((_, reject) => {
      signal.addEventListener('abort', () => reject(signal.reason), { once: true });
    })
  });
  const terminal = await runtime.start('retire', 5);
  assert.equal(terminal.state, 'forceStopped');
  assert.equal(forced, 1);
  assert.equal(states.includes('forceStopped'), true);
});

test('recovery never rolls a published missing or corrupt root back to source', () => {
  assert.equal(classifyRelocationRecovery(false, true, true, true), 'orphan');
  assert.equal(classifyRelocationRecovery(true, false, true, true), 'relocationDataLost');
  assert.equal(classifyRelocationRecovery(true, true, false, true), 'relocationDataLost');
  assert.equal(classifyRelocationRecovery(true, true, true, false), 'relocationDataLost');
  assert.equal(classifyRelocationRecovery(true, true, true, true), 'resume');
});

test('relocation envelope preserves queued work and logical timers deterministically', () => {
  const envelope = relocationEnvelope();
  const encoded = encodeServiceRelocationEnvelope(envelope);
  const decoded = decodeServiceRelocationEnvelope(encoded);
  assert.deepEqual(
    decoded.participants.map(({ key }) => key),
    ['actor:a', 'spot:room']
  );
  assert.deepEqual(
    decoded.queuedMessages.map(({ sequence }) => sequence),
    [1n, 2n]
  );
  assert.deepEqual(
    decoded.timers.map(({ timerId }) => timerId),
    ['heartbeat', 'idle']
  );
  assert.deepEqual(
    encodeServiceRelocationEnvelope(decoded),
    encoded
  );
});

test('mailbox seal captures queued work, holds new ingress, and restores or relays in order', () => {
  const mailbox = new ServiceMailbox({
    applicationMessages: 16,
    applicationBytes: 1_024,
    infrastructureMessages: 4,
    infrastructureBytes: 256
  });
  assert.equal(mailbox.tryEnqueue(mailboxRecord('spot:room', 'application', 'one')), true);
  assert.equal(mailbox.tryEnqueue(mailboxRecord('spot:room', 'application', 'two')), true);
  assert.equal(mailbox.tryEnqueue(mailboxRecord('node', 'infrastructure', 'probe')), true);
  const first = mailbox.trySealApplicationOwner('spot:room');
  assert.ok(first);
  assert.deepEqual(first.captured.map(firstPart), ['one', 'two']);
  assert.equal(mailbox.tryEnqueue(mailboxRecord('spot:room', 'application', 'three')), true);
  assert.equal(mailbox.tryClaim('application', 16, 1_024), undefined);
  const infrastructure = mailbox.tryClaim('infrastructure', 4, 256);
  assert.ok(infrastructure);
  assert.equal(firstPart(infrastructure.records[0]!), 'probe');
  assert.equal(mailbox.release(infrastructure), true);

  assert.equal(mailbox.abortRelocation(first), true);
  const restored = mailbox.tryClaim('application', 16, 1_024);
  assert.ok(restored);
  assert.deepEqual(restored.records.map(firstPart), ['one', 'two', 'three']);
  assert.equal(mailbox.release(restored), true);

  assert.equal(mailbox.tryEnqueue(mailboxRecord('spot:room', 'application', 'four')), true);
  const second = mailbox.trySealApplicationOwner('spot:room');
  assert.ok(second);
  assert.equal(mailbox.tryEnqueue(mailboxRecord('spot:room', 'application', 'five')), true);
  const relay = mailbox.commitRelocation(second);
  assert.deepEqual(relay?.map(firstPart), ['five']);
  assert.equal(mailbox.pendingMessages('application'), 0);
  assert.equal(mailbox.tryEnqueue(mailboxRecord('spot:room', 'application', 'six')), false);
  mailbox.close();
});

test('managed timer pauses and restores its logical schedule without native handles', async () => {
  const options = {
    overrunPolicy: ZLinkTimerOverrunPolicy.CatchUpBounded,
    maxCatchUpTicks: 3,
    stopOnUnhandledException: true
  };
  const source = new ZLinkManagedTimer('heartbeat', 60_000, options, async () => {});
  const captured = await source.captureRelocation();
  assert.equal(captured.name, 'heartbeat');
  assert.equal(captured.periodMs, 60_000);
  assert.equal(captured.pendingTicks, 0);

  const target = new ZLinkManagedTimer('heartbeat', 60_000, options, async () => {});
  target.restoreRelocation(captured);
  const restored = await target.captureRelocation();
  assert.deepEqual(restored, captured);
  await source.dispose();
  await target.dispose();
});

test('durable relocation stores payload before Location CAS and clears authority before delete', async () => {
  const events: string[] = [];
  const authority = new InMemoryServiceLocationAuthority(() => 100);
  const initial = authority.compareExchange(
    'spot:room',
    { kind: 'missing' },
    { kind: 'newObject', payload: Buffer.from('owner-state') }
  );
  assert.equal(initial.kind, 'stored');
  if (initial.kind !== 'stored') return;
  const authorityPort = {
    read: (key: string) => authority.read(key),
    compareExchange: (...args: Parameters<InMemoryServiceLocationAuthority['compareExchange']>) => {
      events.push('authority-cas');
      return authority.compareExchange(...args);
    }
  };
  const store = new MemoryRelocationStore(events);
  const runtime = new ServiceDurableRelocationRuntime(authorityPort, store, authorityCodec);
  const published = await runtime.captureAndPublish('spot:room', initial, relocationEnvelope());
  assert.deepEqual(events.slice(0, 2), ['payload-put', 'authority-cas']);
  assert.equal(published.authority.objectGeneration, initial.objectGeneration);
  assert.equal(
    published.authority.authorityOwnerGeneration,
    initial.authorityOwnerGeneration
  );
  const restored = await runtime.restore(published.authority);
  assert.deepEqual(
    restored.queuedMessages.map(({ sequence }) => sequence),
    [1n, 2n]
  );
  assert.equal(restored.timers[0]?.pendingTicks, 1);

  events.length = 0;
  const released = await runtime.release('spot:room', published.authority);
  assert.deepEqual(events, ['authority-cas', 'payload-delete']);
  assert.equal(authorityCodec.read(released.payload), undefined);
});

test('failed publication removes only the orphan and published data loss never rolls back', async () => {
  const events: string[] = [];
  const authority = new InMemoryServiceLocationAuthority(() => 100);
  const initial = authority.compareExchange(
    'actor:a',
    { kind: 'missing' },
    { kind: 'newObject', payload: Buffer.from('owner-state') }
  );
  assert.equal(initial.kind, 'stored');
  if (initial.kind !== 'stored') return;
  authority.compareExchange(
    'actor:a',
    { kind: 'snapshot', storeVersion: initial.storeVersion },
    { kind: 'preserve', payload: Buffer.from('concurrent-update') }
  );
  const store = new MemoryRelocationStore(events);
  const runtime = new ServiceDurableRelocationRuntime(authority, store, authorityCodec);
  await assert.rejects(
    runtime.captureAndPublish('actor:a', initial, relocationEnvelope()),
    /rejected relocation publication/
  );
  assert.deepEqual(events, ['payload-put', 'payload-delete']);

  const current = authority.read('actor:a');
  assert.equal(current.kind, 'snapshot');
  if (current.kind !== 'snapshot') return;
  const published = await runtime.captureAndPublish('actor:a', current, relocationEnvelope());
  await store.delete(published.publication.reference);
  await assert.rejects(
    runtime.restore(published.authority),
    ServiceRelocationDataLostError
  );
  const afterLoss = authority.read('actor:a');
  assert.equal(afterLoss.kind, 'snapshot');
  if (afterLoss.kind === 'snapshot') {
    assert.equal(
      authorityCodec.read(afterLoss.payload)?.reference,
      published.publication.reference
    );
  }
});

test('authority response loss reconciles a committed publication without deleting its root', async () => {
  const events: string[] = [];
  const authority = new InMemoryServiceLocationAuthority(() => 100);
  const initial = authority.compareExchange(
    'spot:response-loss',
    { kind: 'missing' },
    { kind: 'newObject', payload: Buffer.from('owner-state') }
  );
  assert.equal(initial.kind, 'stored');
  if (initial.kind !== 'stored') return;
  const authorityPort = {
    read: (key: string) => authority.read(key),
    compareExchange: (...args: Parameters<InMemoryServiceLocationAuthority['compareExchange']>) => {
      events.push('authority-cas');
      const committed = authority.compareExchange(...args);
      assert.equal(committed.kind, 'stored');
      throw new Error('authority response lost');
    }
  };
  const store = new MemoryRelocationStore(events);
  const runtime = new ServiceDurableRelocationRuntime(authorityPort, store, authorityCodec);

  const published = await runtime.captureAndPublish(
    'spot:response-loss',
    initial,
    relocationEnvelope()
  );

  assert.deepEqual(events, ['payload-put', 'authority-cas']);
  assert.equal(
    authorityCodec.read(published.authority.payload)?.reference,
    published.publication.reference
  );
  assert.equal((await store.get(published.publication.reference)).kind, 'found');
});

function relocationEnvelope(): ServiceRelocationEnvelope {
  return {
    participants: [
      {
        key: 'spot:room',
        applicationState: Buffer.from('spot-state'),
        acceptedJournal: Buffer.from('spot-journal')
      },
      {
        key: 'actor:a',
        applicationState: Buffer.from('actor-state'),
        acceptedJournal: Buffer.from('actor-journal')
      }
    ],
    queuedMessages: [
      { sequence: 2n, payload: Buffer.from('second') },
      { sequence: 1n, payload: Buffer.from('first') }
    ],
    timers: [
      {
        timerId: 'idle',
        startedAtUnixMs: 100,
        dueAtUnixMs: 1_000,
        intervalMs: 900,
        deliveryIndex: 0n,
        lastScheduledIndex: 0n,
        overrunPolicy: 'skipLateTicks',
        maxCatchUpTicks: 1,
        stopOnUnhandledException: false,
        pendingTicks: 0
      },
      {
        timerId: 'heartbeat',
        startedAtUnixMs: 100,
        dueAtUnixMs: 500,
        intervalMs: 100,
        deliveryIndex: 3n,
        lastScheduledIndex: 4n,
        overrunPolicy: 'catchUpBounded',
        maxCatchUpTicks: 2,
        stopOnUnhandledException: true,
        pendingTicks: 1
      }
    ]
  };
}

const authorityCodec: ServiceRelocationAuthorityCodec = {
  publish(currentPayload, publication) {
    return Buffer.from(JSON.stringify({
      base: Buffer.from(currentPayload).toString('base64'),
      relocation: publication
    }));
  },
  read(payload) {
    try {
      const value = JSON.parse(Buffer.from(payload).toString()) as {
        readonly relocation?: ServiceRelocationPublication;
      };
      return value.relocation;
    } catch {
      return undefined;
    }
  },
  clear(currentPayload, expectedReference) {
    const value = JSON.parse(Buffer.from(currentPayload).toString()) as {
      readonly base: string;
      readonly relocation?: ServiceRelocationPublication;
    };
    assert.equal(value.relocation?.reference, expectedReference);
    return Buffer.from(value.base, 'base64');
  }
};

class MemoryRelocationStore implements ServiceRelocationStorePort {
  private readonly values = new Map<string, Buffer>();
  private nextReference = 1;

  constructor(private readonly events: string[]) {}

  async put(payload: Uint8Array, retentionMs: number) {
    assert.equal(retentionMs, 24 * 60 * 60 * 1_000);
    this.events.push('payload-put');
    const reference = `root-${this.nextReference++}`;
    const stored = Buffer.from(payload);
    this.values.set(reference, stored);
    return {
      reference,
      checksumCrc32c: crc32c(stored),
      storeNowMs: 100,
      expiresAtMs: 100 + retentionMs
    };
  }

  async get(reference: string) {
    const payload = this.values.get(reference);
    return payload === undefined
      ? { kind: 'missing' as const }
      : { kind: 'found' as const, payload: Buffer.from(payload) };
  }

  async delete(reference: string) {
    this.events.push('payload-delete');
    return this.values.delete(reference) ? 'deleted' as const : 'missing' as const;
  }
}

function mailboxRecord(
  owner: string,
  domain: 'application' | 'infrastructure',
  value: string
) {
  return { owner, domain, parts: [Buffer.from(value)] } as const;
}

function firstPart(record: { readonly parts: readonly Uint8Array[] }): string {
  return Buffer.from(record.parts[0]!).toString();
}
