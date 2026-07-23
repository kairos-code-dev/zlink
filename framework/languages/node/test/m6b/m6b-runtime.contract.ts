import assert from 'node:assert/strict';
import { test } from 'node:test';

import { RequestResult, SubmitResult } from '@zlink-systems/zlink';
import {
  ServiceWireCommand,
  ServiceWireFlag
} from '../../../../runtime/protocol/generated/node/service_wire_constants';
import {
  OperationCancelledError,
  OperationRegistry,
  OperationTimeoutError,
  type OperationClock
} from '../../packages/framework/src/runtime/foundation/operation-registry';
import {
  ReadyDomain,
  ReceiveKind,
  type ReceiveRecord
} from '../../packages/framework/src/runtime/foundation/service-runtime-contracts';
import {
  ZLinkNodeRawMeshBackend
} from '../../packages/framework/src/runtime/backend/node/node-raw-mesh-backend';
import type {
  RawServiceMeshRuntime
} from '../../packages/framework/src/runtime/foundation/raw-service-mesh-runtime';
import {
  ServiceStatefulRuntime
} from '../../packages/framework/src/runtime/foundation/service-stateful-runtime';
import {
  ServiceStaleGenerationError,
  ServiceStatefulRegistry,
  ServiceTerminalOperationRegistry
} from '../../packages/framework/src/runtime/foundation/service-stateful-registry';
import {
  M6bServiceWireCommand,
  M6bServiceWireFlag,
  decodeStatefulHeader,
  decodeStatefulReply,
  encodeActorHeader,
  encodeBoundSessionBindHeader,
  encodeSpotHeader,
  encodeStatefulReply
} from '../../packages/framework/src/runtime/foundation/service-stateful-wire-codec';

test('M6B command and flag constants match the generated service wire schema', () => {
  for (const name of Object.keys(M6bServiceWireCommand) as Array<keyof typeof M6bServiceWireCommand>) {
    assert.equal(M6bServiceWireCommand[name], ServiceWireCommand[name]);
  }
  for (const name of Object.keys(M6bServiceWireFlag) as Array<keyof typeof M6bServiceWireFlag>) {
    assert.equal(M6bServiceWireFlag[name], ServiceWireFlag[name]);
  }
});

test('Spot and Actor wire records preserve identity and reject malformed records', () => {
  const spot = {
    spotRid: 'spot-a',
    generation: 7n
  };
  const spotHeader = encodeSpotHeader('spotRequest', 'source', {
    spot,
    targetNodeRid: 'node-b',
    targetNodeGeneration: 3n,
    authorityOwnerGeneration: 9n
  }, 11n);
  assert.deepEqual(decodeStatefulHeader(spotHeader), {
    kind: 'spotRequest',
    correlation: 11n,
    sourceSpotRid: 'source',
    target: {
      spot,
      targetNodeRid: 'node-b',
      targetNodeGeneration: 3n,
      authorityOwnerGeneration: 9n
    }
  });

  const target = { nodeRid: 'node-b', actorId: 'actor-a', generation: 5n };
  const source = { nodeRid: 'node-a', actorId: 'actor-source', generation: 2n };
  const actorHeader = encodeActorHeader(
    'actorRequest',
    {
      actor: target,
      targetNodeGeneration: 3n,
      authorityOwnerGeneration: 5n
    },
    12n,
    source,
    { sessionRid: 'session-a', bindingGeneration: 4n, sequence: 8n }
  );
  assert.deepEqual(decodeStatefulHeader(actorHeader), {
    kind: 'actorRequest',
    correlation: 12n,
    sourceActor: { nodeRid: '', actorId: source.actorId, generation: source.generation },
    target: {
      actor: target,
      targetNodeGeneration: 3n,
      authorityOwnerGeneration: 5n
    },
    boundSession: {
      sessionRid: 'session-a',
      bindingGeneration: 4n,
      sequence: 8n
    }
  });
  assert.throws(() => decodeStatefulHeader(actorHeader.subarray(0, -1)));
  assert.throws(() => decodeStatefulHeader(Buffer.concat([spotHeader, Buffer.of(0)])));
});

test('stateful replies preserve operation-specific tails', () => {
  const actor = { nodeRid: 'node-b', actorId: 'actor-a', generation: 5n };
  const encoded = encodeStatefulReply(17n, RequestResult.Ok, 0, {
    kind: 'actorLookup',
    actor,
    spot: { spotRid: 'spot-a', generation: 7n },
    membershipEpoch: 4n,
    authorityOwnerGeneration: 8n
  });
  assert.deepEqual(decodeStatefulReply(encoded, 17n, 'actorLookup'), {
    correlation: 17n,
    terminalResult: RequestResult.Ok,
    failureCode: 0,
    tail: {
      kind: 'actorLookup',
      actor: { nodeRid: '', actorId: 'actor-a', generation: 5n },
      spot: { spotRid: 'spot-a', generation: 7n },
      membershipEpoch: 4n,
      authorityOwnerGeneration: 8n
    }
  });
  assert.throws(() => decodeStatefulReply(encoded, 18n, 'actorLookup'));
});

test('outbound stateful routes use resolved authority generations and never object generations', () => {
  const sent: Array<{ readonly target: string; readonly parts: readonly Buffer[] }> = [];
  const raw = {
    topology: {
      peer: (nodeRid: string) => nodeRid === 'node-b'
        ? { descriptor: { lifecycleGeneration: 7n } }
        : undefined
    },
    setServiceIngress: () => {},
    sendService: (target: string, parts: readonly Buffer[]) => {
      sent.push({ target, parts });
      return true;
    }
  } as unknown as RawServiceMeshRuntime;
  const runtime = new ServiceStatefulRuntime(raw, 'node-a', 3n);
  const payload = {
    packetName: 'AuthorityFence',
    contentType: 'application/octet-stream',
    payload: Buffer.from('payload')
  };
  const actor = { nodeRid: 'node-b', actorId: 'actor-a', generation: 5n };
  assert.equal(runtime.sendToActor(actor, 7n, actor.generation, payload), SubmitResult.NotFound);
  runtime.rememberActorRoute({
    actor,
    targetNodeGeneration: 7n,
    authorityOwnerGeneration: 11n
  });
  assert.equal(runtime.sendToActor(actor, 7n, actor.generation, payload), SubmitResult.Ok);
  const actorHeader = decodeStatefulHeader(sent.at(-1)!.parts[0]!);
  assert.equal(actorHeader.kind, 'actorSend');
  if (actorHeader.kind === 'actorSend') {
    assert.equal(actorHeader.target.authorityOwnerGeneration, 11n);
  }

  const spot = { spotRid: 'spot-a', generation: 6n };
  assert.equal(runtime.sendToSpot('source', 'node-b', spot, 7n, spot.generation, payload), SubmitResult.NotFound);
  runtime.rememberSpotRoute({
    spot,
    targetNodeRid: 'node-b',
    targetNodeGeneration: 7n,
    authorityOwnerGeneration: 13n
  });
  assert.equal(runtime.sendToSpot('source', 'node-b', spot, 7n, spot.generation, payload), SubmitResult.Ok);
  const spotHeader = decodeStatefulHeader(sent.at(-1)!.parts[0]!);
  assert.equal(spotHeader.kind, 'spotSend');
  if (spotHeader.kind === 'spotSend') {
    assert.equal(spotHeader.target.authorityOwnerGeneration, 13n);
  }
  runtime.close();
});

test('global Spot and Actor identities fence stale generations and retain stable type', () => {
  const registry = new ServiceStatefulRegistry('node-a', 4n);
  const spotV1 = registry.createSpot('spot-a', 'user', 'Room');
  const actorV1 = registry.createActor('actor-a', 'Player', spotV1.ref);
  assert.equal(registry.closeSpot(spotV1.ref), false);
  registry.destroyActor(actorV1.ref);
  assert.equal(registry.closeSpot(spotV1.ref), true);

  const spotV2 = registry.createSpot('spot-a', 'user', 'Room');
  const actorV2 = registry.createActor('actor-a', 'Player', spotV2.ref);
  assert.equal(spotV2.ref.generation, 2n);
  assert.equal(actorV2.ref.generation, 2n);
  assert.throws(() => registry.requireSpot(spotV1.ref), ServiceStaleGenerationError);
  assert.throws(() => registry.requireActor(actorV1.ref), ServiceStaleGenerationError);
  registry.destroyActor(actorV2.ref);
  assert.throws(() => registry.createActor('actor-a', 'Enemy', spotV2.ref), TypeError);
});

test('remote create reservations are idempotent per attempt and fence stale attempts', () => {
  const target = new ServiceStatefulRegistry('node-target', 2n);
  const first = target.reserve('instanceSpot', 'tenant-42', 'TenantWorker', 10n);
  assert.equal(first.kind, 'reserved');
  if (first.kind !== 'reserved') return;
  const duplicate = target.reserve('instanceSpot', 'tenant-42', 'TenantWorker', 10n);
  assert.deepEqual(duplicate, first);

  const newer = target.reserve('instanceSpot', 'tenant-42', 'TenantWorker', 11n);
  assert.equal(newer.kind, 'reserved');
  if (newer.kind !== 'reserved') return;
  assert.equal(target.reserve('instanceSpot', 'tenant-42', 'TenantWorker', 10n).kind, 'attemptStale');
  assert.throws(() => target.commitReservation(first.reservation), ServiceStaleGenerationError);
  const committed = target.commitReservation(newer.reservation);
  assert.equal('kind' in committed ? committed.kind : undefined, 'instance');
  assert.equal(target.reserve('instanceSpot', 'tenant-42', 'OtherWorker', 12n).kind, 'typeMismatch');
});

test('membership and session binding generations advance without losing the binding', () => {
  const registry = new ServiceStatefulRegistry('node-a', 1n);
  const firstSpot = registry.createSpot('spot-a');
  const secondSpot = registry.createSpot('spot-b');
  const actor = registry.createActor('actor-a', 'Player', firstSpot.ref);
  const binding = registry.bindSession(actor.ref, 'session-a', 'node-session');
  const transition = registry.joinActor(actor.ref, secondSpot.ref);
  assert.equal(transition.previousMembershipEpoch, 1n);
  assert.equal(transition.currentMembershipEpoch, 2n);
  assert.equal(registry.binding(actor.ref)?.membershipEpoch, 2n);
  assert.equal(registry.validateBoundSession(actor.ref, binding.bindingGeneration).sessionRid, 'session-a');
  assert.throws(
    () => registry.unbindSession(actor.ref, binding.bindingGeneration + 1n),
    ServiceStaleGenerationError
  );
  assert.equal(registry.unbindSession(actor.ref, binding.bindingGeneration), true);
});

test('Spot and Actor turns serialize per owner while independent owners progress', async () => {
  const registry = new ServiceStatefulRegistry('node-a', 1n);
  const events: string[] = [];
  let releaseFirst!: () => void;
  const firstBarrier = new Promise<void>(resolve => {
    releaseFirst = resolve;
  });
  const first = registry.runTurn('spot:a', async () => {
    events.push('a1-start');
    await firstBarrier;
    events.push('a1-end');
  });
  const second = registry.runTurn('spot:a', () => {
    events.push('a2');
  });
  const independent = registry.runTurn('spot:b', () => {
    events.push('b1');
  });

  await independent;
  assert.deepEqual(events, ['a1-start', 'b1']);
  releaseFirst();
  await Promise.all([first, second]);
  assert.deepEqual(events, ['a1-start', 'b1', 'a1-end', 'a2']);
});

test('reply, timeout and shutdown races settle each Promise exactly once', async () => {
  const clock = new ManualClock();
  const operations = new ServiceTerminalOperationRegistry<number>(
    new OperationRegistry<number>(clock)
  );

  const replyWins = operations.reserve(10);
  assert.equal(operations.reply(replyWins.id, 7), true);
  clock.fireAll();
  assert.equal(await replyWins.promise, 7);
  assert.equal(operations.reply(replyWins.id, 8), false);

  const timeoutWins = operations.reserve(10);
  const timeoutResult = assert.rejects(timeoutWins.promise, OperationTimeoutError);
  clock.fireAll();
  await timeoutResult;
  assert.equal(operations.reply(timeoutWins.id, 9), false);

  const shutdownWins = operations.reserve(10);
  const shutdownResult = assert.rejects(shutdownWins.promise, OperationCancelledError);
  operations.close();
  clock.fireAll();
  await shutdownResult;
});

test('bound session transition wire format fences the binding generation', () => {
  const header = encodeBoundSessionBindHeader(
    23n,
    {
      actor: { nodeRid: 'node-a', actorId: 'actor-a', generation: 2n },
      targetNodeGeneration: 4n,
      authorityOwnerGeneration: 6n
    },
    'session-a',
    { state: 'tombstone', retiredGeneration: 9n }
  );
  assert.deepEqual(decodeStatefulHeader(header), {
    kind: 'boundSessionBind',
    correlation: 23n,
    actor: {
      actor: { nodeRid: 'node-a', actorId: 'actor-a', generation: 2n },
      targetNodeGeneration: 4n,
      authorityOwnerGeneration: 6n
    },
    sessionRid: 'session-a',
    binding: { state: 'tombstone', retiredGeneration: 9n }
  });
});

test('raw backend dispatches Spot requests and Actor sends through M6B owners', async () => {
  const nonce = `${process.pid}-${Date.now()}`;
  const endpoint = `ipc:///tmp/zlink-m6b-node-${nonce}.sock`;
  const backend = new ZLinkNodeRawMeshBackend('m6b-mesh', 'm6b-node');
  backend.setBind(endpoint);
  backend.start();
  try {
    const targetSpot = backend.getOrCreateSpot('spot-target').spot;
    const sourceSpot = backend.entrySpot();
    const operation = sourceSpot.requestToSpot(
      'm6b-node',
      'spot-target',
      targetSpot.status().lifecycleGeneration,
      Buffer.from('spot-request'),
      { timeoutMs: 2_000 }
    );
    const receivedSpot = await drainOne(backend, ReadyDomain.Application);
    assert.equal(receivedSpot.kind, ReceiveKind.SpotRequest);
    assert.equal(String(receivedSpot.sourceSpotRid), 'm6b-node');
    assert.equal(receivedSpot.parts[0]!.toBytes().toString(), 'spot-request');
    assert.equal(receivedSpot.reply(Buffer.from('spot-reply')), SubmitResult.Ok);
    closeParts(receivedSpot);

    const completion = await drainOne(backend, ReadyDomain.Infrastructure);
    assert.deepEqual(completion.operationId, operation);
    assert.equal(completion.terminalResult, RequestResult.Ok);
    assert.equal(completion.parts[0]!.toBytes().toString(), 'spot-reply');
    closeParts(completion);

    const actor = backend.createActor('actor-target');
    assert.equal(backend.sendToActor(actor, Buffer.from('actor-send')), SubmitResult.Ok);
    const receivedActor = await drainOne(backend, ReadyDomain.Application);
    assert.equal(receivedActor.kind, ReceiveKind.ActorSend);
    assert.equal(receivedActor.kindData, null);
    assert.equal(receivedActor.parts[0]!.toBytes().toString(), 'actor-send');
    closeParts(receivedActor);

    const instanceRoute = {
      targetNodeRid: 'm6b-node',
      targetNodeGeneration: 1n,
      targetSpotRid: 'instance-tenant-42',
      objectGeneration: 1n,
      ownerId: 'owner-a',
      authorityOwnerGeneration: 1n,
      leaseGeneration: 1n,
      storeVersion: 'store-v1'
    };
    backend.registerInstanceIntent('TenantWorker', instanceRoute);
    const instanceOperation = backend.requestInstanceSpot(
      instanceRoute,
      Buffer.from('instance-request'),
      2_000,
      'm6b-node'
    );
    const instance = await drainOne(backend, ReadyDomain.Application);
    assert.equal(instance.kind, ReceiveKind.InstanceSpotActivation);
    assert.equal(instance.parts[0]!.toBytes().toString(), 'instance-request');
    assert.equal(instance.reply(Buffer.from('instance-reply')), SubmitResult.Ok);
    closeParts(instance);
    const instanceCompletion = await drainOne(backend, ReadyDomain.Infrastructure);
    assert.deepEqual(instanceCompletion.operationId, instanceOperation);
    assert.equal(instanceCompletion.parts[0]!.toBytes().toString(), 'instance-reply');
    closeParts(instanceCompletion);

    const delivered: Buffer[] = [];
    const sessionService = backend.createStreamSessionService(createFakeStream(delivered));
    sessionService.start();
    const bindOperation = sessionService.bindActor('session-a', actor, 2_000);
    const bindCompletion = await drainOne(backend, ReadyDomain.Infrastructure);
    assert.deepEqual(bindCompletion.operationId, bindOperation);
    assert.equal(bindCompletion.terminalResult, RequestResult.Ok);
    const binding = sessionService.bindings('session-a')[0]!;
    assert.equal(
      backend.sendActorBoundSession(actor, binding.bindingGeneration, Buffer.from('session-message')),
      SubmitResult.Ok
    );
    assert.deepEqual(delivered.map(value => value.toString()), ['session-message']);
    const unbindOperation = sessionService.unbindActor(
      'session-a',
      actor,
      binding.bindingGeneration,
      2_000
    );
    const unbindCompletion = await drainOne(backend, ReadyDomain.Infrastructure);
    assert.deepEqual(unbindCompletion.operationId, unbindOperation);
    assert.equal(unbindCompletion.terminalResult, RequestResult.Ok);
    assert.equal(sessionService.bindings('session-a').length, 0);
    sessionService.close();

    targetSpot.setSubscription('events', 'room-*');
    const publisher = backend.createPublisher();
    const detail = publisher.publish('events', 'room-42', Buffer.from('multicast'));
    assert.equal(detail.snapshotLocalSpotCount, 1);
    assert.equal(detail.admittedLocalSpotCount, 1);
    const multicast = await drainOne(backend, ReadyDomain.Application);
    assert.equal(multicast.kind, ReceiveKind.SpotMulticast);
    assert.equal(multicast.channelName, 'events');
    assert.equal(multicast.topic, 'room-42');
    assert.equal(multicast.parts[0]!.toBytes().toString(), 'multicast');
    closeParts(multicast);
    publisher.close();

    const staleGeneration = targetSpot.status().lifecycleGeneration;
    targetSpot.close();
    const staleOperation = sourceSpot.requestToSpot(
      'm6b-node',
      'spot-target',
      staleGeneration,
      Buffer.from('stale'),
      { timeoutMs: 2_000 }
    );
    const staleCompletion = await drainOne(backend, ReadyDomain.Infrastructure);
    assert.deepEqual(staleCompletion.operationId, staleOperation);
    assert.equal(staleCompletion.terminalResult, RequestResult.NotFound);
    assert.equal(staleCompletion.failureErrno, 21);
  } finally {
    backend.close();
  }
});

class ManualClock implements OperationClock {
  private readonly callbacks = new Map<number, () => void>();
  private nextHandle = 1;

  setTimeout(callback: () => void, _delayMs: number): number {
    const handle = this.nextHandle++;
    this.callbacks.set(handle, callback);
    return handle;
  }

  clearTimeout(handle: unknown): void {
    this.callbacks.delete(handle as number);
  }

  fireAll(): void {
    const callbacks = [...this.callbacks.values()];
    this.callbacks.clear();
    for (const callback of callbacks) callback();
  }
}

async function drainOne(
  backend: ZLinkNodeRawMeshBackend,
  domain: number
): Promise<ReceiveRecord> {
  let ready = backend.createReadyBatch(4);
  await pollUntil(() => {
    ready.close();
    ready = backend.createReadyBatch(4);
    return backend.drainReady(domain, ready).records.length > 0;
  });
  const claim = ready.takeClaim(0);
  const receive = backend.createReceiveBatch(4, 8, 64 * 1024);
  const result = claim.recvBatch(receive);
  assert.equal(result.ok, true);
  assert.equal(result.records.length, 1);
  claim.release();
  receive.close();
  ready.close();
  return result.records[0]!;
}

async function pollUntil(condition: () => boolean): Promise<void> {
  const deadline = Date.now() + 2_000;
  while (Date.now() < deadline) {
    if (condition()) return;
    await new Promise(resolve => setTimeout(resolve, 1));
  }
  throw new Error('Timed out waiting for M6B runtime progress.');
}

function closeParts(record: ReceiveRecord): void {
  for (const part of record.parts) part.close();
}

function createFakeStream(delivered: Buffer[]): unknown {
  return {
    send() {
      const submit = {
        message(part: Uint8Array) {
          delivered.push(Buffer.from(part));
          return submit;
        },
        submit() {
          return true;
        }
      };
      return submit;
    }
  };
}
