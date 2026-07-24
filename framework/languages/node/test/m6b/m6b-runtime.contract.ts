import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { test } from 'node:test';

import { Message, RequestResult, SubmitResult } from '@zlink-systems/zlink';
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
  ServiceInstanceActivationRedirectError,
  ServiceStatefulRuntime,
  type ServiceAsyncInstanceActivationAuthority,
  type ServiceInstanceActivationAuthority
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
  encodeInstanceSpotActivationHeader,
  encodeInstanceSpotHeader,
  encodeSpotHeader,
  encodeStatefulReply,
  encodeUserSpotCloseHeader,
  encodeUserSpotCreateHeader,
  type ServiceInstanceRouteFence
} from '../../packages/framework/src/runtime/foundation/service-stateful-wire-codec';
import {
  encodeApplicationPayload
} from '../../packages/framework/src/runtime/foundation/service-wire-m6a-codec';
import { crc32c } from '../../packages/framework/src/runtime/foundation/service-relocation-runtime';
import {
  decodeServiceReadySpotAuthority,
  encodeServiceInstanceAuthorityPayload
} from '../../packages/framework/src/runtime/foundation/service-authority-payload-codec';
import {
  decodeInstanceActivationRecoveryEnvelope,
  encodeInstanceActivationRecoveryEnvelope
} from '../../packages/framework/src/runtime/foundation/service-instance-activation-recovery-codec';
import {
  encodeServiceMetadataFrame,
  validateServiceMetadataFrame
} from '../../packages/framework/src/runtime/foundation/service-metadata-codec';
import {
  ZLinkStatefulAuthorityRouteRuntime
} from '../../packages/framework/src/runtime/host/stateful-authority-route-runtime';
import {
  ZLinkInstanceActivationAuthority
} from '../../packages/framework/src/runtime/host/instance-activation-authority';
import {
  ZLinkInMemoryAuthorityStore
} from '../../packages/framework/src/runtime/locations/in-memory-authority-store';
import { encodeAuthorityKey } from '../../packages/framework/src/runtime/locations/authority-key-codec';
import type {
  ZLinkAuthorityKey,
  ZLinkAuthoritySnapshot,
  ZLinkAuthorityStore
} from '../../packages/framework/src/contracts/Locations/Authority';
import { ZLinkAuthorityScanCursor } from '../../packages/framework/src/contracts/Locations/Authority';
import type { ZLinkBackendMeshNode } from '../../packages/framework/src/runtime/backend/contracts';
import type {
  ZLinkRelocationReference,
  ZLinkRelocationStore
} from '../../packages/framework/src/contracts/Locations/RelocationStore';
import {
  DefaultZLinkSpotManager,
  DefaultZLinkSpotOutbound,
  ZLinkSpotSerialExecutor
} from '../../packages/framework/src/runtime/spots';
import {
  hasObjectClientCapability,
  ZLinkHostSpotAddressTransport
} from '../../packages/framework/src/runtime/host/spot-address-transport';
import {
  encodeChannelEnvelopeParts,
  encodeChannelReplyParts,
  ZLinkChannelMessageKind
} from '../../packages/framework/src/runtime/channels/channel-envelope';
import {
  ZLinkRuntimeRouteTransport
} from '../../packages/framework/src/runtime/channels/channel-transports';
import type {
  ZLinkInstanceSpot,
  ZLinkInstanceSpotContext,
  ZLinkSpotPacketHandler
} from '../../packages/framework/src/contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkMessageMetadataEmpty,
  ZLinkSubmitStatus
} from '../../packages/framework/src/contracts';

test('M6B command and flag constants match the generated service wire schema', () => {
  for (const name of Object.keys(M6bServiceWireCommand) as Array<keyof typeof M6bServiceWireCommand>) {
    assert.equal(M6bServiceWireCommand[name], ServiceWireCommand[name]);
  }
  for (const name of Object.keys(M6bServiceWireFlag) as Array<keyof typeof M6bServiceWireFlag>) {
    assert.equal(M6bServiceWireFlag[name], ServiceWireFlag[name]);
  }
});

test('remote User Spot create and close records preserve every generation fence exactly', () => {
  const create = encodeUserSpotCreateHeader({
    correlation: 71n,
    operation: { high: 5n, low: 9n },
    sourceNodeRid: 'source',
    sourceNodeGeneration: 11n,
    spotRid: 'spot-1',
    stableType: 'Room',
    reservation: {
      reservationId: 'reservation-1',
      expectedStoreVersion: 'version-1',
      objectGeneration: 13n,
      authorityOwnerGeneration: 17n,
      targetNodeRid: 'target',
      targetNodeGeneration: 19n,
      targetOwnerId: 'owner-1',
      targetOwnerLeaseGeneration: 23n,
      pendingCapacityDelta: 1
    },
    deadlineUnixMs: 29n
  });
  assert.deepEqual(decodeStatefulHeader(create), {
    kind: 'userSpotCreate',
    correlation: 71n,
    operation: { high: 5n, low: 9n },
    sourceNodeRid: 'source',
    sourceNodeGeneration: 11n,
    spotRid: 'spot-1',
    stableType: 'Room',
    reservation: {
      reservationId: 'reservation-1',
      expectedStoreVersion: 'version-1',
      objectGeneration: 13n,
      authorityOwnerGeneration: 17n,
      targetNodeRid: 'target',
      targetNodeGeneration: 19n,
      targetOwnerId: 'owner-1',
      targetOwnerLeaseGeneration: 23n,
      pendingCapacityDelta: 1
    },
    deadlineUnixMs: 29n
  });

  const close = encodeUserSpotCloseHeader({
    correlation: 73n,
    operation: { high: 31n, low: 37n },
    sourceNodeRid: 'source',
    sourceNodeGeneration: 41n,
    target: {
      spotRid: 'spot-1',
      objectGeneration: 43n,
      targetNodeRid: 'target',
      targetNodeGeneration: 47n,
      authorityOwnerGeneration: 53n,
      expectedStoreVersion: 'version-2'
    },
    deadlineUnixMs: 59n
  });
  assert.deepEqual(decodeStatefulHeader(close), {
    kind: 'userSpotClose',
    correlation: 73n,
    operation: { high: 31n, low: 37n },
    sourceNodeRid: 'source',
    sourceNodeGeneration: 41n,
    target: {
      spotRid: 'spot-1',
      objectGeneration: 43n,
      targetNodeRid: 'target',
      targetNodeGeneration: 47n,
      authorityOwnerGeneration: 53n,
      expectedStoreVersion: 'version-2'
    },
    deadlineUnixMs: 59n
  });
  assert.throws(() => decodeStatefulHeader(Buffer.concat([create, Buffer.of(0)])));
  assert.throws(() => decodeStatefulHeader(close.subarray(0, -1)));
});

test('remote User Spot command 20 success tails use operation discriminators 13 and 14', () => {
  assert.deepEqual(
    decodeStatefulReply(
      encodeStatefulReply(79n, RequestResult.Ok, 0, {
        kind: 'userSpotCreate',
        createResult: 'created',
        spotRid: 'spot-2',
        objectGeneration: 83n
      }),
      79n,
      'userSpotCreate'
    ).tail,
    {
      kind: 'userSpotCreate',
      createResult: 'created',
      spotRid: 'spot-2',
      objectGeneration: 83n
    }
  );
  assert.deepEqual(
    decodeStatefulReply(
      encodeStatefulReply(89n, RequestResult.Ok, 0, {
        kind: 'userSpotClose',
        closed: true
      }),
      89n,
      'userSpotClose'
    ).tail,
    { kind: 'userSpotClose', closed: true }
  );
});

test('remote User Spot target executes once and rewrites correlation on terminal replay', async () => {
  let ingress!: (record: {
    readonly command: number;
    readonly flags: number;
    readonly sourceRoutingId: string;
    readonly requestSequence: bigint;
    readonly parts: readonly Buffer[];
  }) => unknown;
  const replies: Array<readonly Buffer[]> = [];
  const raw = {
    topology: {
      peer: (nodeRid: string) => nodeRid === 'source'
        ? { descriptor: { lifecycleGeneration: 5n } }
        : undefined
    },
    setServiceIngress: (handler: typeof ingress) => {
      ingress = handler;
    },
    replyService: (_record: unknown, parts: readonly Buffer[]) => {
      replies.push(parts);
    }
  } as unknown as RawServiceMeshRuntime;
  const runtime = new ServiceStatefulRuntime(raw, 'target', 7n);
  let executions = 0;
  runtime.registerUserSpotOperationHandler({
    create: async record => {
      executions++;
      return {
        terminalResult: RequestResult.Ok,
        failureCode: 0,
        tail: {
          kind: 'userSpotCreate',
          createResult: 'created',
          spotRid: record.spotRid,
          objectGeneration: record.reservation.objectGeneration
        }
      };
    },
    close: async () => {
      throw new Error('not used');
    }
  });
  const request = {
    operation: { high: 11n, low: 13n },
    sourceNodeRid: 'source',
    sourceNodeGeneration: 5n,
    spotRid: 'spot-replay',
    stableType: 'Room',
    reservation: {
      reservationId: 'reservation',
      expectedStoreVersion: 'version',
      objectGeneration: 17n,
      authorityOwnerGeneration: 19n,
      targetNodeRid: 'target',
      targetNodeGeneration: 7n,
      targetOwnerId: 'owner',
      targetOwnerLeaseGeneration: 23n,
      pendingCapacityDelta: 1
    },
    deadlineUnixMs: BigInt(Date.now() + 250)
  };
  for (const [index, correlation] of [29n, 31n].entries()) {
    const header = encodeUserSpotCreateHeader({ ...request, correlation });
    assert.equal(ingress({
      command: M6bServiceWireCommand.userSpotCreate,
      flags: 0,
      sourceRoutingId: 'source',
      requestSequence: BigInt(index + 1),
      parts: [header]
    }), 'infrastructure');
    await new Promise(resolve => setImmediate(resolve));
  }
  assert.equal(executions, 1);
  assert.equal(replies.length, 2);
  assert.equal(
    decodeStatefulReply(replies[0]![0]!, 29n, 'userSpotCreate').tail?.kind,
    'userSpotCreate'
  );
  assert.equal(
    decodeStatefulReply(replies[1]![0]!, 31n, 'userSpotCreate').tail?.kind,
    'userSpotCreate'
  );
  await new Promise(resolve => setTimeout(resolve, 300));
  const expiredReplay = encodeUserSpotCreateHeader({
    ...request,
    correlation: 33n
  });
  assert.equal(ingress({
    command: M6bServiceWireCommand.userSpotCreate,
    flags: 0,
    sourceRoutingId: 'source',
    requestSequence: 3n,
    parts: [expiredReplay]
  }), 'infrastructure');
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(executions, 1);
  assert.equal(
    decodeStatefulReply(replies[2]![0]!, 33n, 'userSpotCreate').tail?.kind,
    'userSpotCreate'
  );
  const expiredNew = encodeUserSpotCreateHeader({
    ...request,
    correlation: 35n,
    operation: { high: 11n, low: 39n },
    deadlineUnixMs: BigInt(Date.now() - 1)
  });
  assert.equal(ingress({
    command: M6bServiceWireCommand.userSpotCreate,
    flags: 0,
    sourceRoutingId: 'source',
    requestSequence: 4n,
    parts: [expiredNew]
  }), 'infrastructure');
  assert.equal(executions, 1);
  assert.equal(
    decodeStatefulReply(replies[3]![0]!, 35n, 'userSpotCreate').terminalResult,
    RequestResult.TimedOut
  );
  const wrongTargetLifecycle = encodeUserSpotCreateHeader({
    ...request,
    correlation: 36n,
    operation: { high: 11n, low: 40n },
    reservation: {
      ...request.reservation,
      targetNodeGeneration: 8n
    },
    deadlineUnixMs: BigInt(Date.now() + 10_000)
  });
  assert.equal(ingress({
    command: M6bServiceWireCommand.userSpotCreate,
    flags: 0,
    sourceRoutingId: 'source',
    requestSequence: 5n,
    parts: [wrongTargetLifecycle]
  }), 'infrastructure');
  assert.equal(executions, 1);
  assert.equal(
    decodeStatefulReply(replies[4]![0]!, 36n, 'userSpotCreate').failureCode,
    34
  );
  const terminalTable = (
    runtime as unknown as {
      admittedUserSpotOperations: Map<string, {
        readonly request: string;
        readonly deadlineUnixMs: bigint;
        readonly result: Promise<unknown>;
        settled: boolean;
      }>;
    }
  ).admittedUserSpotOperations;
  for (let index = terminalTable.size; index < 65_536; index++) {
    terminalTable.set(`occupied-${index}`, {
      request: 'occupied',
      deadlineUnixMs: BigInt(Date.now() + 10_000),
      result: new Promise(() => undefined),
      settled: false
    });
  }
  const overflow = encodeUserSpotCreateHeader({
    ...request,
    correlation: 37n,
    operation: { high: 11n, low: 41n },
    deadlineUnixMs: BigInt(Date.now() + 10_000)
  });
  assert.equal(ingress({
    command: M6bServiceWireCommand.userSpotCreate,
    flags: 0,
    sourceRoutingId: 'source',
    requestSequence: 6n,
    parts: [overflow]
  }), 'infrastructure');
  assert.equal(executions, 1);
  assert.equal(
    decodeStatefulReply(replies[5]![0]!, 37n, 'userSpotCreate').terminalResult,
    RequestResult.Busy
  );
  const originalDateNow = Date.now;
  Date.now = () => originalDateNow() + 5 * 60_000 + 1_000;
  try {
    const retiredReplay = encodeUserSpotCreateHeader({
      ...request,
      correlation: 43n
    });
    assert.equal(ingress({
      command: M6bServiceWireCommand.userSpotCreate,
      flags: 0,
      sourceRoutingId: 'source',
      requestSequence: 7n,
      parts: [retiredReplay]
    }), 'infrastructure');
    assert.equal(executions, 1);
    assert.equal(
      decodeStatefulReply(replies[6]![0]!, 43n, 'userSpotCreate').terminalResult,
      RequestResult.TimedOut
    );
  } finally {
    Date.now = originalDateNow;
  }
  runtime.close();
});

test('authority keys share the Spot discriminator and preserve colon identities canonically', () => {
  assert.equal(
    encodeAuthorityKey('instance_spot', 'tenant:42').value,
    'zla1:s:9:tenant%3A42'
  );
  assert.equal(
    encodeAuthorityKey('user_spot', 'tenant:42').value,
    'zla1:s:9:tenant%3A42'
  );
});

test('authority payload bytes match the schema fixture shape and reject malformed UTF-8', () => {
  const base = {
    stableType: 'TenantWorker',
    spotRid: 'tenant:42',
    ownerId: 'owner-a',
    ownerLeaseGeneration: 5n,
    ownerMeshName: 'mesh-a',
    ownerNodeRid: 'node-a',
    ownerNodeGeneration: 1n
  };
  const cold = encodeServiceInstanceAuthorityPayload({
    ...base,
    state: 'coldActivating'
  });
  const ready = encodeServiceInstanceAuthorityPayload({ ...base, state: 'ready' });
  const activationRecovery = {
    reference: 'relocation:first-message',
    sha256: Buffer.alloc(32, 0x5a),
    encodedSize: 256,
    inboxSequence: 1n,
    replayCursor: 0n
  };
  const recoveringReady = encodeServiceInstanceAuthorityPayload({
    ...base,
    state: 'ready',
    activationRecovery
  });
  assert.deepEqual(
    decodeServiceReadySpotAuthority(recoveringReady)?.activationRecovery,
    activationRecovery
  );
  assert.throws(
    () => encodeServiceInstanceAuthorityPayload({
      ...base,
      state: 'coldActivating',
      activationRecovery
    }),
    /Ready Instance Spot/
  );
  assert.equal(
    cold.toString('hex'),
    '5a4c4155010000000000510102001d03001a0100170c54656e616e74576f726b6572'
      + '0974656e616e743a3432076f776e65722d610000000000000005066d6573682d6106'
      + '6e6f64652d61000000000000000100000000000000000000cfdf6035'
  );
  assert.equal(
    ready.toString('hex'),
    '5a4c4155010000000000510002001d03001a0200170c54656e616e74576f726b6572'
      + '0974656e616e743a3432076f776e65722d610000000000000005066d6573682d6106'
      + '6e6f64652d610000000000000001000000000000000000006761e989'
  );

  const malformed = Buffer.from(ready);
  const spotRidOffset = malformed.indexOf(Buffer.from('tenant:42'));
  assert.notEqual(spotRidOffset, -1);
  malformed[spotRidOffset] = 0xff;
  malformed.writeUInt32BE(crc32c(malformed.subarray(0, -4)), malformed.byteLength - 4);
  assert.equal(decodeServiceReadySpotAuthority(malformed), undefined);

  const closeWithRecovery = Buffer.from(recoveringReady);
  closeWithRecovery[11] = 3;
  closeWithRecovery.writeUInt32BE(
    crc32c(closeWithRecovery.subarray(0, -4)),
    closeWithRecovery.byteLength - 4
  );
  assert.equal(decodeServiceReadySpotAuthority(closeWithRecovery), undefined);

  const actorWithRecovery = Buffer.from(recoveringReady);
  actorWithRecovery[12] = 1;
  actorWithRecovery.writeUInt32BE(
    crc32c(actorWithRecovery.subarray(0, -4)),
    actorWithRecovery.byteLength - 4
  );
  assert.equal(decodeServiceReadySpotAuthority(actorWithRecovery), undefined);

  const userSpotWithRecovery = Buffer.from(recoveringReady);
  userSpotWithRecovery[15] = 2;
  userSpotWithRecovery.writeUInt32BE(
    crc32c(userSpotWithRecovery.subarray(0, -4)),
    userSpotWithRecovery.byteLength - 4
  );
  assert.equal(decodeServiceReadySpotAuthority(userSpotWithRecovery), undefined);

  const recoveryReferenceOffset = recoveringReady.indexOf(Buffer.from(activationRecovery.reference));
  assert.notEqual(recoveryReferenceOffset, -1);
  const activationUnionOffset = recoveryReferenceOffset - 7;
  const relocationUnionOffset = activationUnionOffset - 5;
  const relocationWithRecovery = Buffer.from(recoveringReady);
  relocationWithRecovery[relocationUnionOffset] = 1;
  relocationWithRecovery.writeUInt32BE(
    crc32c(relocationWithRecovery.subarray(0, -4)),
    relocationWithRecovery.byteLength - 4
  );
  assert.equal(decodeServiceReadySpotAuthority(relocationWithRecovery), undefined);
});

test('Instance activation recovery envelope preserves the complete first operation', () => {
  const input = {
    target: {
      targetSpotRid: 'tenant:42',
      stableType: 'TenantWorker',
      targetNodeRid: 'node-b',
      targetNodeGeneration: 7n,
      descriptorVersion: 'descriptor-v3'
    },
    targetMeshName: 'mesh-b',
    sourceNodeRid: 'node-a',
    sourceNodeGeneration: 5n,
    sourceSpotRid: 'source:spot',
    operationKind: 'request' as const,
    operation: { high: 5n, low: 19n },
    replyRouteId: 23n,
    deadlineUnixMs: 99_999n,
    applicationPayload: {
      packetName: 'TenantRequest',
      contentType: 'application/octet-stream',
      payload: Buffer.from('first')
    }
  };
  const encoded = encodeInstanceActivationRecoveryEnvelope(input);
  assert.deepEqual(decodeInstanceActivationRecoveryEnvelope(encoded), input);
  const corrupted = Buffer.from(encoded);
  corrupted[corrupted.byteLength - 1] ^= 0xff;
  assert.throws(
    () => decodeInstanceActivationRecoveryEnvelope(corrupted),
    /checksum/
  );
});

test('Instance activation recovery envelope matches the cross-language golden bytes', () => {
  const encoded = encodeInstanceActivationRecoveryEnvelope({
    target: {
      targetSpotRid: 'spot-1',
      stableType: 'quest',
      targetNodeRid: 'target',
      targetNodeGeneration: 7n,
      descriptorVersion: 'descriptor-9'
    },
    targetMeshName: 'main',
    sourceNodeRid: 'source',
    sourceNodeGeneration: 3n,
    sourceSpotRid: 'entry',
    operationKind: 'request',
    operation: { high: 0n, low: 9n },
    replyRouteId: 11n,
    deadlineUnixMs: 1_700_000_000_000n,
    metadataFrame: encodeServiceMetadataFrame(new Map([['trace', 'abc']])),
    applicationPayload: {
      packetName: 'quest.start',
      contentType: 'application/json',
      payload: Buffer.from('{"x":1}')
    }
  });
  assert.equal(
    encoded.toString('hex'),
    '5a4c4941010000000000a00673706f742d31057175657374046d61696e067461726765'
      + '74'
      + '00000000000000070c64657363726970746f722d3906736f7572636500000000000000'
      + '030105656e7472790200000000000000000000000000000009000000000000000b0000'
      + '018bcfe56800010101057472616365000361626301000000280b71756573742e73746172'
      + '74106170706c69636174696f6e2f6a736f6e000000077b2278223a317de138c97b'
  );
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
      peer: (nodeRid: string) => nodeRid === 'node-b' || nodeRid === 'node-c'
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
  }, 'store-v1');
  assert.equal(runtime.sendToSpot('source', 'node-b', spot, 7n, spot.generation, payload), SubmitResult.Ok);
  const spotHeader = decodeStatefulHeader(sent.at(-1)!.parts[0]!);
  assert.equal(spotHeader.kind, 'spotSend');
  if (spotHeader.kind === 'spotSend') {
    assert.equal(spotHeader.target.authorityOwnerGeneration, 13n);
  }
  runtime.rememberSpotRoute({
    spot,
    targetNodeRid: 'node-c',
    targetNodeGeneration: 7n,
    authorityOwnerGeneration: 13n
  }, 'store-v2');
  runtime.rememberSpotRoute({
    spot,
    targetNodeRid: 'node-b',
    targetNodeGeneration: 7n,
    authorityOwnerGeneration: 12n
  }, 'store-stale-owner');
  runtime.forgetSpotRoute(spot, 13n, 'store-v1');
  assert.equal(runtime.sendToSpot('source', 'node-c', spot, 7n, 13n, payload), SubmitResult.Ok);
  const successorHeader = decodeStatefulHeader(sent.at(-1)!.parts[0]!);
  assert.equal(successorHeader.kind, 'spotSend');
  if (successorHeader.kind === 'spotSend') {
    assert.equal(successorHeader.target.targetNodeRid, 'node-c');
    assert.equal(successorHeader.target.authorityOwnerGeneration, 13n);
  }
  runtime.close();
});

test('Instance activation encoding distinguishes absent metadata from explicit empty metadata', () => {
  const sent: Array<{ readonly target: string; readonly parts: readonly Buffer[] }> = [];
  const raw = {
    topology: {
      peer: () => ({ descriptor: { lifecycleGeneration: 7n } })
    },
    setServiceIngress: () => {},
    sendService: (target: string, parts: readonly Buffer[]) => {
      sent.push({ target, parts });
      return true;
    }
  } as unknown as RawServiceMeshRuntime;
  const runtime = new ServiceStatefulRuntime(raw, 'source', 3n);
  const target = {
    targetNodeRid: 'target',
    targetNodeGeneration: 7n,
    targetSpotRid: 'room',
    stableType: 'Room',
    descriptorVersion: '1'
  };
  const payload = {
    packetName: 'Open',
    contentType: 'application/octet-stream',
    payload: Buffer.from('open')
  };
  runtime.sendToMissingInstanceSpot(
    target,
    payload,
    BigInt(Date.now() + 1_000)
  );
  runtime.sendToMissingInstanceSpot(
    target,
    payload,
    BigInt(Date.now() + 1_000),
    undefined,
    encodeServiceMetadataFrame(new Map())
  );
  assert.equal(sent[0]?.parts.length, 2);
  assert.equal(sent[1]?.parts.length, 3);
  assert.deepEqual(
    validateServiceMetadataFrame(sent[1]!.parts[1]!),
    Buffer.from([1, 0])
  );
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

test('target-owned Instance activation reserves before factory, commits before one queue admission', () => {
  const queued: unknown[] = [];
  let ingress!: (record: {
    readonly command: number;
    readonly flags: number;
    readonly sourceRoutingId: string;
    readonly parts: readonly Buffer[];
  }) => string | undefined;
  const raw = {
    topology: {
      peer: (nodeRid: string) => nodeRid === 'source'
        ? { descriptor: { lifecycleGeneration: 7n } }
        : undefined
    },
    mailbox: {
      tryEnqueue: (record: unknown) => {
        queued.push(record);
        return true;
      }
    },
    setServiceIngress: (handler: typeof ingress) => {
      ingress = handler;
    }
  } as unknown as RawServiceMeshRuntime;
  const runtime = new ServiceStatefulRuntime(raw, 'target', 3n);
  const events: string[] = [];
  let committedRoute: ReturnType<ServiceInstanceActivationAuthority['read']> = { kind: 'missing' };
  const authority: ServiceInstanceActivationAuthority = {
    read: () => {
      events.push('read');
      return committedRoute;
    },
    reserve: () => {
      events.push('reserve');
      assert.equal(runtime.registry.spot('tenant-42'), undefined);
      return {
        kind: 'reserved',
        reservation: { attempt: 11n, token: 'reservation-11' }
      };
    },
    commit: (_target, _reservation, spot) => {
      events.push('commit');
      assert.equal(runtime.registry.spot('tenant-42'), spot);
      const committed = {
        kind: 'committed',
        route: {
          targetNodeRid: 'target',
          targetNodeGeneration: 3n,
          targetSpotRid: 'tenant-42',
          objectGeneration: spot.ref.generation,
          ownerId: 'target',
          authorityOwnerGeneration: spot.authorityOwnerGeneration,
          leaseGeneration: 1n,
          storeVersion: '12'
        }
      } as const;
      committedRoute = { kind: 'ready', route: committed.route };
      return committed;
    },
    abort: () => {
      assert.fail('successful activation must not abort');
    }
  };
  runtime.registerInstanceActivationAuthority(authority);

  const target = {
    targetNodeRid: 'target',
    targetNodeGeneration: 3n,
    targetSpotRid: 'tenant-42',
    stableType: 'TenantWorker',
    descriptorVersion: 'descriptor-5'
  };
  const header = encodeInstanceSpotActivationHeader(
    target,
    7n,
    'source',
    undefined,
    'send',
    { high: 7n, low: 31n },
    BigInt(Date.now() + 10_000)
  );
  const parts = [
    header,
    encodeApplicationPayload({
      packetName: 'FirstMessage',
      contentType: 'application/octet-stream',
      payload: Buffer.from('first')
    })
  ];
  const record = {
    command: M6bServiceWireCommand.instanceSpot,
    flags: 0,
    sourceRoutingId: 'source',
    parts
  };
  assert.equal(ingress(record), 'application');
  assert.deepEqual(events, ['read', 'reserve', 'commit']);
  assert.equal(runtime.registry.spot('tenant-42')?.stableType, 'TenantWorker');
  assert.equal(queued.length, 1);

  assert.equal(ingress(record), 'application');
  assert.deepEqual(events, ['read', 'reserve', 'commit', 'read']);
  assert.equal(queued.length, 1);
  runtime.close();
});

test('Instance activation CAS loser does not invoke the local factory', () => {
  let ingress!: (record: {
    readonly command: number;
    readonly flags: number;
    readonly sourceRoutingId: string;
    readonly parts: readonly Buffer[];
  }) => string | undefined;
  const raw = {
    topology: {
      peer: () => ({ descriptor: { lifecycleGeneration: 7n } })
    },
    mailbox: { tryEnqueue: () => true },
    setServiceIngress: (handler: typeof ingress) => {
      ingress = handler;
    }
  } as unknown as RawServiceMeshRuntime;
  const runtime = new ServiceStatefulRuntime(raw, 'target', 3n);
  const winnerRoute = {
    targetNodeRid: 'other-target',
    targetNodeGeneration: 8n,
    targetSpotRid: 'tenant-42',
    objectGeneration: 2n,
    ownerId: 'other-target',
    authorityOwnerGeneration: 9n,
    leaseGeneration: 4n,
    storeVersion: '19'
  };
  runtime.registerInstanceActivationAuthority({
    read: () => ({ kind: 'missing' }),
    reserve: () => ({ kind: 'ready', route: winnerRoute }),
    commit: () => assert.fail('CAS loser must not commit'),
    abort: () => assert.fail('CAS loser must not abort another owner')
  });
  const header = encodeInstanceSpotActivationHeader(
    {
      targetNodeRid: 'target',
      targetNodeGeneration: 3n,
      targetSpotRid: 'tenant-42',
      stableType: 'TenantWorker',
      descriptorVersion: 'descriptor-5'
    },
    7n,
    'source',
    undefined,
    'send',
    { high: 7n, low: 32n },
    BigInt(Date.now() + 10_000)
  );
  assert.throws(
    () => ingress({
      command: M6bServiceWireCommand.instanceSpot,
      flags: 0,
      sourceRoutingId: 'source',
      parts: [
        header,
        encodeApplicationPayload({
          packetName: 'FirstMessage',
          contentType: 'application/octet-stream',
          payload: Buffer.from('first')
        })
      ]
    }),
    ServiceInstanceActivationRedirectError
  );
  assert.equal(runtime.registry.spot('tenant-42'), undefined);
  runtime.close();
});

test('Promise authority redirects the retained activation envelope to the Ready winner', async () => {
  let ingress!: (record: {
    readonly command: number;
    readonly flags: number;
    readonly sourceRoutingId: string;
    readonly parts: readonly Buffer[];
  }) => string | undefined;
  let redirected:
    | { readonly targetNodeRid: string; readonly parts: readonly Buffer[] }
    | undefined;
  const raw = {
    topology: {
      peer: () => ({ descriptor: { lifecycleGeneration: 7n } })
    },
    mailbox: { tryEnqueue: () => assert.fail('CAS loser must not admit locally') },
    sendService: (targetNodeRid: string, parts: readonly Buffer[]) => {
      redirected = { targetNodeRid, parts };
      return true;
    },
    setServiceIngress: (handler: typeof ingress) => {
      ingress = handler;
    }
  } as unknown as RawServiceMeshRuntime;
  const runtime = new ServiceStatefulRuntime(raw, 'loser', 3n);
  runtime.registerAsyncInstanceActivationAuthority({
    read: async () => ({
      kind: 'ready',
      route: {
        targetNodeRid: 'winner',
        targetNodeGeneration: 9n,
        targetSpotRid: 'tenant-redirect',
        objectGeneration: 4n,
        ownerId: 'winner-owner',
        authorityOwnerGeneration: 6n,
        leaseGeneration: 2n,
        storeVersion: 'ready-v1'
      }
    }),
    reserve: async () => assert.fail('Ready authority must not reserve'),
    resume: async () => assert.fail('Ready authority must not resume'),
    commit: async () => assert.fail('Ready authority must not commit'),
    complete: async () => assert.fail('Redirected activation must not complete locally'),
    abort: async () => assert.fail('Ready authority must not abort')
  });

  const operation = { high: 7n, low: 44n };
  assert.equal(ingress({
    command: M6bServiceWireCommand.instanceSpot,
    flags: 0,
    sourceRoutingId: 'source',
    parts: [
      encodeInstanceSpotActivationHeader(
        {
          targetNodeRid: 'loser',
          targetNodeGeneration: 3n,
          targetSpotRid: 'tenant-redirect',
          stableType: 'TenantWorker',
          descriptorVersion: 'descriptor-loser'
        },
        7n,
        'source',
        'source-spot',
        'send',
        operation,
        BigInt(Date.now() + 10_000)
      ),
      encodeApplicationPayload({
        packetName: 'FirstMessage',
        contentType: 'application/octet-stream',
        payload: Buffer.from('first')
      })
    ]
  }), 'infrastructure');
  await new Promise<void>(resolve => setImmediate(resolve));

  assert.equal(redirected?.targetNodeRid, 'winner');
  if (redirected === undefined) throw new Error('Activation envelope was not redirected.');
  const redirectedHeader = decodeStatefulHeader(redirected.parts[0]!);
  assert.equal(redirectedHeader.kind, 'instanceSpot');
  if (redirectedHeader.kind !== 'instanceSpot') throw new Error('Redirect header is invalid.');
  assert.equal(redirectedHeader.activation, 'missing');
  assert.deepEqual(redirectedHeader.operation, operation);
  assert.equal(redirectedHeader.sourceNodeRid, 'loser');
  assert.equal(redirectedHeader.sourceNodeGeneration, 3n);
  assert.equal(redirectedHeader.sourceSpotRid, 'source-spot');
  if (redirectedHeader.activation === 'missing') {
    assert.equal(redirectedHeader.target.targetNodeRid, 'winner');
    assert.equal(redirectedHeader.target.targetNodeGeneration, 9n);
  }
  runtime.close();
});

test('Promise authority resumes the retained activation envelope after Store completion', async () => {
  let ingress!: (record: {
    readonly command: number;
    readonly flags: number;
    readonly sourceRoutingId: string;
    readonly parts: readonly Buffer[];
  }) => string | undefined;
  const queued: unknown[] = [];
  const raw = {
    topology: {
      peer: () => ({ descriptor: { lifecycleGeneration: 7n } })
    },
    mailbox: {
      tryEnqueue: (record: unknown) => {
        queued.push(record);
        return true;
      }
    },
    setServiceIngress: (handler: typeof ingress) => {
      ingress = handler;
    }
  } as unknown as RawServiceMeshRuntime;
  const runtime = new ServiceStatefulRuntime(raw, 'target', 3n);
  const events: string[] = [];
  let committedRoute: ServiceInstanceRouteFence | undefined;
  let releaseRead!: () => void;
  const readBarrier = new Promise<void>(resolve => {
    releaseRead = resolve;
  });
  const authority: ServiceAsyncInstanceActivationAuthority = {
    read: async () => {
      events.push('read');
      await readBarrier;
      return { kind: 'missing' };
    },
    reserve: async (activation) => {
      events.push('reserve');
      assert.deepEqual(
        activation.metadataFrame,
        encodeServiceMetadataFrame(new Map([['trace', 'activation-1']]))
      );
      return {
        kind: 'reserved',
        reservation: { attempt: 12n, token: 'reservation-12' }
      };
    },
    resume: async () => assert.fail('Live activation must not resume a startup reservation'),
    commit: async (_target, _reservation, spot) => {
      events.push('commit');
      const committed = {
        kind: 'committed',
        route: {
          targetNodeRid: 'target',
          targetNodeGeneration: 3n,
          targetSpotRid: 'tenant-async',
          objectGeneration: spot.ref.generation,
          ownerId: 'target',
          authorityOwnerGeneration: spot.authorityOwnerGeneration,
          leaseGeneration: 1n,
          storeVersion: '20'
        }
      } as const;
      committedRoute = committed.route;
      return committed;
    },
    complete: async (_target, route) => {
      events.push('complete');
      return route;
    },
    abort: async () => {
      assert.fail('successful activation must not abort');
    }
  };
  runtime.registerAsyncInstanceActivationAuthority(authority);

  const activationMetadata = encodeServiceMetadataFrame(
    new Map([['trace', 'activation-1']])
  );
  const header = encodeInstanceSpotActivationHeader(
    {
      targetNodeRid: 'target',
      targetNodeGeneration: 3n,
      targetSpotRid: 'tenant-async',
      stableType: 'TenantWorker',
      descriptorVersion: 'descriptor-5'
    },
    7n,
    'source',
    undefined,
    'send',
    { high: 7n, low: 33n },
    BigInt(Date.now() + 10_000),
    undefined,
    true
  );
  assert.equal(ingress({
    command: M6bServiceWireCommand.instanceSpot,
    flags: M6bServiceWireFlag.metadata,
    sourceRoutingId: 'source',
    parts: [
      header,
      activationMetadata,
      encodeApplicationPayload({
        packetName: 'FirstMessage',
        contentType: 'application/octet-stream',
        payload: Buffer.from('first')
      })
    ]
  }), 'infrastructure');
  assert.deepEqual(events, ['read']);
  assert.equal(queued.length, 0);

  releaseRead();
  await new Promise<void>(resolve => setImmediate(resolve));
  assert.deepEqual(events, ['read', 'reserve', 'commit']);
  assert.equal(runtime.registry.spot('tenant-async')?.stableType, 'TenantWorker');
  assert.equal(queued.length, 1);
  const firstRecord = queued[0] as {
    readonly stateful?: {
      readonly applicationMetadata?: Buffer;
      readonly onTerminalCompletion?: () => Promise<void>;
    };
  };
  assert.deepEqual(firstRecord.stateful?.applicationMetadata, activationMetadata);
  assert.ok(firstRecord.stateful?.onTerminalCompletion);
  await firstRecord.stateful!.onTerminalCompletion!();
  assert.deepEqual(events, ['read', 'reserve', 'commit', 'complete']);
  if (committedRoute === undefined) throw new Error('Ready route was not committed.');
  const instanceIntent = (
    runtime as unknown as {
      readonly instanceIntents: ReadonlyMap<
        string,
        { readonly route: ServiceInstanceRouteFence }
      >
    }
  ).instanceIntents.get('tenant-async');
  assert.deepEqual(instanceIntent?.route, committedRoute);
  const followingHeader = encodeInstanceSpotHeader(
    committedRoute,
    7n,
    'source',
    undefined,
    'send',
    { high: 0n, low: 0n }
  );
  assert.equal(ingress({
    command: M6bServiceWireCommand.instanceSpot,
    flags: 0,
    sourceRoutingId: 'source',
    parts: [
      followingHeader,
      encodeApplicationPayload({
        packetName: 'FollowingMessage',
        contentType: 'application/octet-stream',
        payload: Buffer.from('following')
      })
    ]
  }), 'application');
  assert.equal(queued.length, 2);
  runtime.close();
});

test('Instance application factory initializes before the first recovered handler turn', async () => {
  const events: string[] = [];
  class FirstMessageHandler implements ZLinkSpotPacketHandler<ZLinkInstanceSpot, { value: number }> {
    async handle(_spot: ZLinkInstanceSpot, message: { value: number }): Promise<void> {
      events.push(`handle:${message.value}`);
    }
  }
  class TenantInstance implements ZLinkInstanceSpot {
    declare readonly context: ZLinkInstanceSpotContext;

    configure(): void {
      events.push('configure');
      this.context.handlers.addPacket(FirstMessageHandler);
    }

    async onInitialize(): Promise<void> {
      events.push('initialize');
    }
  }
  const manager = new DefaultZLinkSpotManager({
    spotFactories: [],
    instanceSpotFactories: new Map([
      ['mesh-a', new Map([['TenantWorker', TenantInstance]])]
    ])
  });
  await manager.materializeInstance('mesh-a', 'TenantWorker', 'tenant:factory');
  const parts = encodeChannelEnvelopeParts(
    ZLinkChannelMessageKind.Command,
    'instance',
    FirstMessageHandler.name,
    { value: 7 }
  ).map(part => Message.from(part));
  try {
    await manager.dispatchMeshInstance(
      'mesh-a',
      {
        ownerKind: 2,
        domain: ReadyDomain.Application,
        spotRid: 'tenant:factory',
        actor: null
      },
      {
        kind: ReceiveKind.InstanceSpotActivation,
        domain: ReadyDomain.Application,
        sourceNodeRid: null,
        sourceSpotRid: null,
        sourceBindingGeneration: 0n,
        sourceActor: null,
        operationId: { high: 1n, low: 1n },
        operationKind: 0,
        channelName: null,
        topic: null,
        applicationMetadata: null,
        kindData: null,
        terminalResult: 0,
        failureErrno: 0,
        parts,
        reply: () => SubmitResult.InvalidState,
        replyActorJoin: () => SubmitResult.NotSupported
      }
    );
  } finally {
    for (const part of parts) part.close();
  }
  assert.deepEqual(events, ['configure', 'initialize', 'handle:7']);
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

test('authority reconciliation exact-reads complete scans and publishes only Ready mesh-local routes', async () => {
  const readyV1 = instanceAuthoritySnapshot({
    spotRid: 'tenant:42',
    meshName: 'mesh-b',
    nodeRid: 'node-b',
    storeVersion: 'store-v1',
    authorityOwnerGeneration: 7n,
    state: 'ready'
  });
  const cold = instanceAuthoritySnapshot({
    spotRid: 'tenant:cold',
    meshName: 'mesh-a',
    nodeRid: 'node-a',
    storeVersion: 'store-cold',
    authorityOwnerGeneration: 3n,
    state: 'coldActivating'
  });
  const store = new ReconcileAuthorityStore([
    ['row:tenant:42', readyV1],
    ['row:tenant:cold', cold]
  ]);
  const nodeA = new RecordingAuthorityNode('mesh-a', 'node-a');
  const nodeB = new RecordingAuthorityNode('mesh-b', 'node-b');
  const runtime = new ZLinkStatefulAuthorityRouteRuntime({
    store,
    meshNodes: new Map([
      ['mesh-a', nodeA as unknown as ZLinkBackendMeshNode],
      ['mesh-b', nodeB as unknown as ZLinkBackendMeshNode]
    ]),
    pollingIntervalMs: 60_000,
    pageSize: 1,
    reportError: (error) => {
      throw error;
    }
  });

  await runtime.reconcile();
  assert.deepEqual(store.readKeys, ['row:tenant:42', 'row:tenant:cold']);
  assert.equal(nodeA.remembered.length, 0);
  assert.equal(nodeB.remembered.length, 1);
  assert.equal(nodeB.remembered[0]!.route.spot.spotRid, 'tenant:42');
  assert.equal(nodeB.intents[0]!.route.storeVersion, 'store-v1');

  const readyV2 = instanceAuthoritySnapshot({
    spotRid: 'tenant:42',
    meshName: 'mesh-b',
    nodeRid: 'node-b',
    storeVersion: 'store-v2',
    authorityOwnerGeneration: 7n,
    state: 'ready'
  });
  store.replace('row:tenant:42', readyV2);
  await runtime.reconcile();
  assert.equal(nodeB.intents.at(-1)!.route.storeVersion, 'store-v2');
  assert.deepEqual(nodeB.forgottenIntents.at(-1), {
    spotRid: 'tenant:42',
    authorityOwnerGeneration: 7n,
    storeVersion: 'store-v1'
  });

  store.scanExpired = true;
  store.replace('row:tenant:42', cold);
  const cleanupCount = nodeB.forgottenIntents.length;
  await runtime.reconcile();
  assert.equal(nodeB.forgottenIntents.length, cleanupCount);

  store.scanExpired = false;
  await runtime.reconcile();
  assert.equal(nodeB.forgottenIntents.at(-1)!.storeVersion, 'store-v2');
});

test('authority reconciliation refuses startup when the recovery scan expires', async () => {
  const store = new ReconcileAuthorityStore([]);
  store.scanExpired = true;
  const runtime = new ZLinkStatefulAuthorityRouteRuntime({
    store,
    meshNodes: new Map(),
    pollingIntervalMs: 60_000,
    pageSize: 1,
    reportError: () => undefined
  });

  await assert.rejects(
    runtime.start(),
    /initial authority recovery scan expired/
  );
});

test('authority reconciliation restores the durable Instance inbox before startup returns', async () => {
  const recoveryEnvelope = encodeInstanceActivationRecoveryEnvelope({
    target: {
      targetSpotRid: 'tenant:recover',
      stableType: 'TenantWorker',
      targetNodeRid: 'node-a',
      targetNodeGeneration: 1n,
      descriptorVersion: '1'
    },
    targetMeshName: 'mesh-a',
    sourceNodeRid: 'source-a',
    sourceNodeGeneration: 2n,
    operationKind: 'send',
    operation: { high: 1n, low: 7n },
    deadlineUnixMs: 99_999n,
    applicationPayload: {
      packetName: 'TenantRequest',
      contentType: 'application/octet-stream',
      payload: Buffer.from('recover')
    }
  });
  const snapshot = instanceAuthoritySnapshot({
    spotRid: 'tenant:recover',
    meshName: 'mesh-a',
    nodeRid: 'node-a',
    storeVersion: 'store-recovery',
    authorityOwnerGeneration: 3n,
    state: 'ready',
    activationRecovery: {
      reference: 'relocation:recover',
      sha256: createHash('sha256').update(recoveryEnvelope).digest(),
      encodedSize: recoveryEnvelope.byteLength,
      inboxSequence: 1n,
      replayCursor: 0n
    }
  });
  const node = new RecordingAuthorityNode('mesh-a', 'node-a');
  const runtime = new ZLinkStatefulAuthorityRouteRuntime({
    store: new ReconcileAuthorityStore([['row:tenant:recover', snapshot]]),
    relocationStore: {
      putRelocation: async () => assert.fail('Recovery must not write a second root.'),
      getRelocation: async () => ({ kind: 'found', payload: recoveryEnvelope }),
      renewRelocation: async () => ({ kind: 'missing' }),
      deleteRelocation: async () => assert.fail('The authority adapter owns root deletion.')
    },
    meshNodes: new Map([['mesh-a', node as unknown as ZLinkBackendMeshNode]]),
    pollingIntervalMs: 60_000,
    pageSize: 10,
    reportError: (error) => {
      throw error;
    }
  });

  await runtime.reconcile(undefined, true);
  assert.equal(node.recovered.length, 1);
  assert.equal(node.recovered[0]!.envelope.target.targetSpotRid, 'tenant:recover');
  assert.equal(node.recovered[0]!.route.storeVersion, 'store-recovery');

  const staleDescriptorNode = new RecordingAuthorityNode(
    'mesh-a',
    'node-a',
    undefined,
    2n
  );
  const staleDescriptorRuntime = new ZLinkStatefulAuthorityRouteRuntime({
    store: new ReconcileAuthorityStore([['row:tenant:recover', snapshot]]),
    relocationStore: {
      putRelocation: async () => assert.fail('Recovery must not write a second root.'),
      getRelocation: async () => ({ kind: 'found', payload: recoveryEnvelope }),
      renewRelocation: async () => ({ kind: 'missing' }),
      deleteRelocation: async () => assert.fail('The authority adapter owns root deletion.')
    },
    meshNodes: new Map([
      ['mesh-a', staleDescriptorNode as unknown as ZLinkBackendMeshNode]
    ]),
    pollingIntervalMs: 60_000,
    pageSize: 10,
    reportError: () => undefined
  });
  await staleDescriptorRuntime.reconcile(undefined, true);
  assert.equal(staleDescriptorNode.recovered.length, 0);
  assert.equal(staleDescriptorNode.forgotten.length, 1);
});

test('authority reconciliation resumes an exact Pending Instance reservation', async () => {
  const recoveryEnvelope = encodeInstanceActivationRecoveryEnvelope({
    target: {
      targetSpotRid: 'tenant:pending',
      stableType: 'TenantWorker',
      targetNodeRid: 'node-a',
      targetNodeGeneration: 1n,
      descriptorVersion: '1'
    },
    targetMeshName: 'mesh-a',
    sourceNodeRid: 'source-a',
    sourceNodeGeneration: 2n,
    operationKind: 'send',
    operation: { high: 1n, low: 8n },
    deadlineUnixMs: 99_999n,
    applicationPayload: {
      packetName: 'TenantRequest',
      contentType: 'application/octet-stream',
      payload: Buffer.from('pending')
    }
  });
  const requestSha256 = createHash('sha256').update(recoveryEnvelope).digest();
  const snapshot = instanceAuthoritySnapshot({
    spotRid: 'tenant:pending',
    meshName: 'mesh-a',
    nodeRid: 'node-a',
    storeVersion: 'store-pending',
    authorityOwnerGeneration: 3n,
    state: 'coldActivating',
    pendingCreation: {
      reservationId: 'reservation-pending',
      requestContentReference: 'relocation:pending',
      requestSha256,
      requestEncodedSize: BigInt(recoveryEnvelope.byteLength)
    }
  });
  const node = new RecordingAuthorityNode('mesh-a', 'node-a');
  const runtime = new ZLinkStatefulAuthorityRouteRuntime({
    store: new ReconcileAuthorityStore([['row:tenant:pending', snapshot]]),
    relocationStore: {
      putRelocation: async () => assert.fail('Recovery must not write a second root.'),
      getRelocation: async () => ({ kind: 'found', payload: recoveryEnvelope }),
      renewRelocation: async () => ({ kind: 'missing' }),
      deleteRelocation: async () => assert.fail('The authority adapter owns root deletion.')
    },
    meshNodes: new Map([['mesh-a', node as unknown as ZLinkBackendMeshNode]]),
    pollingIntervalMs: 60_000,
    pageSize: 10,
    reportError: (error) => {
      throw error;
    }
  });

  await runtime.reconcile(undefined, true);
  assert.equal(node.recoveredPending.length, 1);
  assert.equal(
    node.recoveredPending[0]!.pending.reservationId,
    'reservation-pending'
  );
  assert.equal(
    node.recoveredPending[0]!.envelope.target.targetSpotRid,
    'tenant:pending'
  );
});

test('production Instance authority adapter writes schema ColdActivating then Ready payloads', async () => {
  const store = new ZLinkInMemoryAuthorityStore({ isTargetLive: () => true });
  const compareExchangeAuthority = store.compareExchangeAuthority.bind(store);
  let preserveAttempts = 0;
  store.compareExchangeAuthority = async (key, expected, mutation, signal) => {
    preserveAttempts += 1;
    if (preserveAttempts === 2) {
      throw new Error('simulated crash before recovery pointer release');
    }
    return await compareExchangeAuthority(key, expected, mutation, signal);
  };
  let recordedRequestReference: string | undefined;
  const reserve = store.reserve.bind(store);
  store.reserve = async (request, signal) => {
    recordedRequestReference = request.intent.requestContentReference;
    return await reserve(request, signal);
  };
  let storedRequest: Uint8Array | undefined;
  const requestReference = {
    value: 'relocation:first-message'
  } as ZLinkRelocationReference;
  const relocationStore: ZLinkRelocationStore = {
    putRelocation: async (payload) => {
      storedRequest = Buffer.from(payload);
      const now = new Date();
      return {
        reference: requestReference,
        checksumCrc32c: crc32c(payload),
        expiresAt: new Date(now.getTime() + 60_000),
        storeNow: now
      };
    },
    getRelocation: async () => storedRequest === undefined
      ? { kind: 'missing' }
      : { kind: 'found', payload: storedRequest },
    renewRelocation: async () => ({ kind: 'missing' }),
    deleteRelocation: async () => {
      throw new Error('simulated orphan cleanup failure');
    }
  };
  const authority = new ZLinkInstanceActivationAuthority({
    store,
    relocationStore,
    meshName: 'mesh-a',
    owner: () => ({ ownerId: 'owner-a', leaseGeneration: 5n })
  });
  const target = {
    targetNodeRid: 'node-a',
    targetNodeGeneration: 1n,
    targetSpotRid: 'tenant:42',
    stableType: 'TenantWorker',
    descriptorVersion: 'descriptor-v1'
  };
  assert.deepEqual(await authority.read(target), { kind: 'missing' });
  const reserved = await authority.reserve({
    target,
    sourceNodeRid: 'source-a',
    sourceNodeGeneration: 2n,
    operationKind: 'send',
    operation: { high: 1n, low: 2n },
    deadlineUnixMs: 99_999n,
    applicationPayload: {
      packetName: 'TenantRequest',
      contentType: 'application/octet-stream',
      payload: Buffer.from('create')
    }
  });
  assert.equal(reserved.kind, 'reserved');
  if (reserved.kind !== 'reserved') throw new Error('Instance reservation failed.');
  const creating = await store.readAuthority({
    value: encodeAuthorityKey('instance_spot', 'tenant:42').value
  } as ZLinkAuthorityKey);
  assert.equal(creating.kind, 'snapshot');
  if (creating.kind !== 'snapshot') throw new Error('Creating authority is missing.');
  assert.equal(decodeServiceReadySpotAuthority(creating.payload), undefined);
  assert.ok(storedRequest !== undefined);
  assert.equal(recordedRequestReference, requestReference.value);
  assert.equal(
    decodeInstanceActivationRecoveryEnvelope(storedRequest!).targetMeshName,
    'mesh-a'
  );

  const resumedAuthority = new ZLinkInstanceActivationAuthority({
    store,
    relocationStore,
    meshName: 'mesh-a',
    owner: () => ({ ownerId: 'owner-a', leaseGeneration: 5n })
  });
  const projection = creating.pendingCreation;
  if (projection === undefined) throw new Error('Pending creation projection is missing.');
  const resumed = await resumedAuthority.resume(target, {
    reservationId: projection.reservationId,
    storeVersion: creating.storeVersion.value,
    objectGeneration: creating.objectGeneration,
    authorityOwnerGeneration: creating.authorityOwnerGeneration,
    ownerId: creating.ownerId,
    ownerLeaseGeneration: creating.ownerLeaseGeneration,
    meshName: creating.allocation.descriptor.meshName,
    nodeRid: String(creating.allocation.descriptor.rid),
    nodeGeneration: creating.allocation.descriptorLifecycleGeneration,
    requestReference: projection.requestContentReference,
    requestSha256: projection.requestSha256,
    requestEncodedSize: projection.requestEncodedSize
  });
  assert.deepEqual(resumed, reserved.reservation);
  const committed = await resumedAuthority.commit(
    target,
    resumed,
    {
      kind: 'instance',
      stableType: 'TenantWorker',
      ref: { spotRid: 'tenant:42', generation: reserved.reservation.attempt },
      authorityOwnerGeneration: creating.authorityOwnerGeneration
    } as never
  );
  assert.equal(committed.kind, 'committed');
  const committedSnapshot = await store.readAuthority({
    value: encodeAuthorityKey('instance_spot', 'tenant:42').value
  } as ZLinkAuthorityKey);
  assert.equal(committedSnapshot.kind, 'snapshot');
  if (committedSnapshot.kind !== 'snapshot') throw new Error('Ready authority is missing.');
  assert.equal(
    decodeServiceReadySpotAuthority(committedSnapshot.payload)?.activationRecovery?.reference,
    requestReference.value
  );
  const ready = await resumedAuthority.read(target);
  assert.equal(ready.kind, 'ready');
  if (ready.kind === 'ready') {
    assert.equal(ready.route.targetSpotRid, 'tenant:42');
    assert.equal(ready.route.storeVersion, committed.route.storeVersion);
  }
  await assert.rejects(
    resumedAuthority.complete(target, committed.route),
    /simulated crash before recovery pointer release/
  );
  const terminalRecorded = await store.readAuthority({
    value: encodeAuthorityKey('instance_spot', 'tenant:42').value
  } as ZLinkAuthorityKey);
  assert.equal(terminalRecorded.kind, 'snapshot');
  if (terminalRecorded.kind !== 'snapshot') {
    throw new Error('Terminal completion authority is missing.');
  }
  assert.deepEqual(
    decodeServiceReadySpotAuthority(terminalRecorded.payload)?.activationRecovery,
    {
      reference: requestReference.value,
      sha256: createHash('sha256').update(storedRequest!).digest(),
      encodedSize: storedRequest!.byteLength,
      inboxSequence: 1n,
      replayCursor: 1n
    }
  );
  assert.ok(storedRequest !== undefined);

  // A restarted authority observes the durable cursor and performs only the
  // pointer release; it must not need to repeat the terminal handler.
  store.compareExchangeAuthority = compareExchangeAuthority;
  const restartedAuthority = new ZLinkInstanceActivationAuthority({
    store,
    relocationStore,
    meshName: 'mesh-a',
    owner: () => ({ ownerId: 'owner-a', leaseGeneration: 5n })
  });
  let recoveryReleaseCount = 0;
  const recoveryNode = new RecordingAuthorityNode(
    'mesh-a',
    'node-a',
    async (recoveryTarget, route) => {
      recoveryReleaseCount += 1;
      return await restartedAuthority.complete(recoveryTarget, route);
    }
  );
  const recoveryRuntime = new ZLinkStatefulAuthorityRouteRuntime({
    store,
    relocationStore,
    meshNodes: new Map([
      ['mesh-a', recoveryNode as unknown as ZLinkBackendMeshNode]
    ]),
    pollingIntervalMs: 60_000,
    pageSize: 10,
    reportError: (error) => {
      throw error;
    }
  });
  await recoveryRuntime.reconcile(undefined, true);
  assert.equal(recoveryReleaseCount, 1);
  assert.equal(recoveryNode.recovered.length, 0);
  const releasedSnapshot = await store.readAuthority({
    value: encodeAuthorityKey('instance_spot', 'tenant:42').value
  } as ZLinkAuthorityKey);
  assert.equal(releasedSnapshot.kind, 'snapshot');
  if (releasedSnapshot.kind !== 'snapshot') throw new Error('Released authority is missing.');
  assert.notEqual(releasedSnapshot.storeVersion.value, committed.route.storeVersion);
  assert.equal(
    decodeServiceReadySpotAuthority(releasedSnapshot.payload)?.activationRecovery,
    undefined
  );
  assert.ok(storedRequest !== undefined);
});

test('production Instance Ready commit Store rejection is exposed as RequestFailed with the original cause', async () => {
  const store = new ZLinkInMemoryAuthorityStore({ isTargetLive: () => true });
  const requestPayloads = new Map<string, Uint8Array>();
  let sequence = 0;
  const relocationStore: ZLinkRelocationStore = {
    putRelocation: async (payload) => {
      const value = `relocation:commit-fault:${++sequence}`;
      requestPayloads.set(value, Buffer.from(payload));
      const now = new Date();
      return {
        reference: { value } as ZLinkRelocationReference,
        checksumCrc32c: crc32c(payload),
        expiresAt: new Date(now.getTime() + 60_000),
        storeNow: now
      };
    },
    getRelocation: async (reference) => {
      const payload = requestPayloads.get(reference.value);
      return payload === undefined
        ? { kind: 'missing' }
        : { kind: 'found', payload };
    },
    renewRelocation: async () => ({ kind: 'missing' }),
    deleteRelocation: async (reference) => requestPayloads.delete(reference.value)
      ? 'deleted'
      : 'missing'
  };
  const authority = new ZLinkInstanceActivationAuthority({
    store,
    relocationStore,
    meshName: 'mesh-a',
    owner: () => ({ ownerId: 'owner-a', leaseGeneration: 5n })
  });
  const target = {
    targetNodeRid: 'node-a',
    targetNodeGeneration: 1n,
    targetSpotRid: 'tenant:commit-fault',
    stableType: 'TenantWorker',
    descriptorVersion: 'descriptor-v1'
  };
  const reserved = await authority.reserve({
    target,
    sourceNodeRid: 'source-a',
    sourceNodeGeneration: 2n,
    operationKind: 'send',
    operation: { high: 1n, low: 2n },
    deadlineUnixMs: 99_999n,
    applicationPayload: {
      packetName: 'TenantRequest',
      contentType: 'application/octet-stream',
      payload: Buffer.from('create')
    }
  });
  assert.equal(reserved.kind, 'reserved');
  if (reserved.kind !== 'reserved') return;
  const creating = await store.readAuthority({
    value: encodeAuthorityKey('instance_spot', target.targetSpotRid).value
  } as ZLinkAuthorityKey);
  assert.equal(creating.kind, 'snapshot');
  if (creating.kind !== 'snapshot') return;
  const commitFault = new Error('Instance commit unavailable');
  store.commit = async () => {
    throw commitFault;
  };
  await assert.rejects(
    () => authority.commit(target, reserved.reservation, {
      kind: 'instance',
      stableType: target.stableType,
      ref: {
        spotRid: target.targetSpotRid,
        generation: reserved.reservation.attempt
      },
      authorityOwnerGeneration: creating.authorityOwnerGeneration
    } as never),
    (error: unknown) => error instanceof ZLinkFrameworkException
      && error.kind === ZLinkFrameworkErrorKind.RequestFailed
      && error.cause === commitFault
  );
});

test('concurrent Instance activation CAS loser joins Ready and returns the winner route', async () => {
  const store = new ZLinkInMemoryAuthorityStore({ isTargetLive: () => true });
  const roots = new Map<string, Buffer>();
  let nextReference = 0;
  const relocationStore: ZLinkRelocationStore = {
    putRelocation: async (payload) => {
      const reference = {
        value: `relocation:concurrent:${++nextReference}`
      } as ZLinkRelocationReference;
      const bytes = Buffer.from(payload);
      roots.set(reference.value, bytes);
      const now = new Date();
      return {
        reference,
        checksumCrc32c: crc32c(bytes),
        expiresAt: new Date(now.getTime() + 60_000),
        storeNow: now
      };
    },
    getRelocation: async (reference) => {
      const payload = roots.get(reference.value);
      return payload === undefined
        ? { kind: 'missing' }
        : { kind: 'found', payload };
    },
    renewRelocation: async () => ({ kind: 'missing' }),
    deleteRelocation: async () => {
      throw new Error('simulated CAS-loser orphan cleanup failure');
    }
  };
  const winnerAuthority = new ZLinkInstanceActivationAuthority({
    store,
    relocationStore,
    meshName: 'mesh-a',
    owner: () => ({ ownerId: 'owner-a', leaseGeneration: 1n })
  });
  const loserAuthority = new ZLinkInstanceActivationAuthority({
    store,
    relocationStore,
    meshName: 'mesh-a',
    owner: () => ({ ownerId: 'owner-b', leaseGeneration: 1n })
  });
  const winnerTarget = {
    targetNodeRid: 'winner-node',
    targetNodeGeneration: 3n,
    targetSpotRid: 'tenant:concurrent',
    stableType: 'TenantWorker',
    descriptorVersion: 'winner-descriptor'
  };
  const loserTarget = {
    ...winnerTarget,
    targetNodeRid: 'loser-node',
    targetNodeGeneration: 4n,
    descriptorVersion: 'loser-descriptor'
  };
  const activation = {
    sourceNodeRid: 'source',
    sourceNodeGeneration: 7n,
    operationKind: 'send' as const,
    operation: { high: 9n, low: 1n },
    deadlineUnixMs: BigInt(Date.now() + 2_000),
    applicationPayload: {
      packetName: 'FirstMessage',
      contentType: 'application/octet-stream',
      payload: Buffer.from('concurrent')
    }
  };
  const winner = await winnerAuthority.reserve({
    ...activation,
    target: winnerTarget
  });
  assert.equal(winner.kind, 'reserved');
  if (winner.kind !== 'reserved') throw new Error('Winner did not reserve activation.');

  const loser = loserAuthority.reserve({
    ...activation,
    target: loserTarget
  });
  await new Promise<void>(resolve => setImmediate(resolve));
  const creating = await store.readAuthority(
    encodeAuthorityKey('instance_spot', winnerTarget.targetSpotRid)
  );
  assert.equal(creating.kind, 'snapshot');
  if (creating.kind !== 'snapshot') throw new Error('Winner reservation is missing.');
  const committed = await winnerAuthority.commit(
    winnerTarget,
    winner.reservation,
    {
      kind: 'instance',
      stableType: winnerTarget.stableType,
      ref: {
        spotRid: winnerTarget.targetSpotRid,
        generation: winner.reservation.attempt
      },
      authorityOwnerGeneration: creating.authorityOwnerGeneration
    } as never
  );
  assert.equal(committed.kind, 'committed');

  const joined = await loser;
  assert.equal(joined.kind, 'ready');
  if (joined.kind !== 'ready') throw new Error('CAS loser did not join Ready.');
  assert.equal(joined.route.targetNodeRid, winnerTarget.targetNodeRid);
  assert.equal(joined.route.targetNodeGeneration, winnerTarget.targetNodeGeneration);
  assert.equal(joined.route.storeVersion, committed.route.storeVersion);
  assert.ok(
    new ServiceInstanceActivationRedirectError(joined.route)
      instanceof ServiceInstanceActivationRedirectError
  );
  assert.equal(roots.size, 2);
});

test('raw backend dispatches Spot requests and Actor sends through M6B owners', async () => {
  const nonce = `${process.pid}-${Date.now()}`;
  const endpoint = `ipc:///tmp/zlink-m6b-node-${nonce}.sock`;
  const backend = new ZLinkNodeRawMeshBackend('m6b-mesh', 'm6b-node');
  backend.setBind(endpoint);
  backend.start();
  const instanceRoute = {
    targetNodeRid: 'm6b-node',
    targetNodeGeneration: backend.status().lifecycleGeneration,
    targetSpotRid: 'tenant:42',
    objectGeneration: 1n,
    ownerId: 'owner-a',
    authorityOwnerGeneration: 1n,
    leaseGeneration: 1n,
    storeVersion: 'store-v1'
  };
  const authorityRoutes = new ZLinkStatefulAuthorityRouteRuntime({
    store: singleAuthorityStore(
      'canonical-authority:tenant:42',
      {
        kind: 'snapshot',
        storeVersion: { value: instanceRoute.storeVersion } as ZLinkAuthoritySnapshot['storeVersion'],
        payload: encodeServiceInstanceAuthorityPayload({
          state: 'ready',
          stableType: 'TenantWorker',
          spotRid: instanceRoute.targetSpotRid,
          ownerId: instanceRoute.ownerId,
          ownerLeaseGeneration: instanceRoute.leaseGeneration,
          ownerMeshName: 'm6b-mesh',
          ownerNodeRid: instanceRoute.targetNodeRid,
          ownerNodeGeneration: instanceRoute.targetNodeGeneration
        }),
        objectGeneration: instanceRoute.objectGeneration,
        authorityOwnerGeneration: instanceRoute.authorityOwnerGeneration,
        ownerId: instanceRoute.ownerId,
        ownerLeaseGeneration: instanceRoute.leaseGeneration,
        allocation: {
          state: 'active',
          objectKind: 'instance_spot',
          stableType: 'TenantWorker',
          descriptor: {
            meshName: 'm6b-mesh',
            rid: instanceRoute.targetNodeRid
          },
          descriptorLifecycleGeneration: instanceRoute.targetNodeGeneration,
          capacityDelta: 1
        },
        storeNow: new Date()
      }
    ),
    meshNodes: new Map([['m6b-mesh', backend]]),
    pollingIntervalMs: 60_000,
    pageSize: 100,
    reportError: (error) => {
      throw error;
    }
  });
  await authorityRoutes.start();
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
    await authorityRoutes.stop();
    backend.close();
  }
});

test('public SpotRid call reaches production host Missing Instance placement without raw runtime access', async () => {
  const submissions: Array<{
    readonly target: {
      readonly targetNodeRid: string;
      readonly targetNodeGeneration: bigint;
      readonly targetSpotRid: string;
      readonly stableType: string;
      readonly descriptorVersion: string;
    };
    readonly deadline: bigint;
    readonly metadata?: ReadonlyMap<string, string>;
  }> = [];
  let requested = false;
  const node = {
    selectObjectPlacement(stableType: string) {
      assert.equal(stableType, 'chat-room');
      return {
        targetNodeRid: 'node-b',
        targetNodeGeneration: 7n,
        descriptorVersion: '11'
      };
    },
    sendToMissingInstanceSpot(
      target: (typeof submissions)[number]['target'],
      _parts: unknown,
      deadline: bigint,
      _source?: string,
      metadata?: ReadonlyMap<string, string>
    ) {
      submissions.push({ target, deadline, metadata });
      return SubmitResult.Ok;
    },
    requestToMissingInstanceSpot() {
      requested = true;
      return { high: 9n, low: 1n };
    }
  } as unknown as ZLinkBackendMeshNode;
  const address = new ZLinkHostSpotAddressTransport({
    resolver: () => ({
      async resolve() {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.SpotRouteNotFound,
          'missing'
        );
      }
    }),
    routed: {
      async sendToSpot() {
        throw new Error('Ready route must not be used for Missing authority.');
      },
      async requestToSpot() {
        throw new Error('request is not used by this test.');
      }
    },
    meshNames: () => ['mesh'],
    instanceTypes: () => ['chat-room'],
    meshNode: () => node,
    completions: () => ({
      async wait() {
        return {
          terminalResult: 0,
          failureErrno: 0,
          operationKind: 39,
          kindData: null,
          parts: encodeChannelReplyParts({
            formatMarker: 0xf2,
            kind: ZLinkChannelMessageKind.Request,
            channelName: 'mesh',
            messageName: 'Ping',
            contentType: 'application/json',
            correlationId: '1',
            deadline: null,
            topic: null,
            metadata: {}
          }, 'ready-reply').map(part =>
            part instanceof Message ? part : Message.from(part)
          )
        };
      }
    }) as never,
    defaultRequestTimeoutMs: 5_000
  });
  const outbound = new DefaultZLinkSpotOutbound(
    new ZLinkSpotSerialExecutor(),
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    undefined,
    'mesh',
    undefined,
    address
  );

  class Notice {
    readonly text = 'hello';
  }
  const result = await outbound.sendToSpot('instance-42', new Notice())
    .metadata('trace', 'abc')
    .instanceSpot('chat-room')
    .inMesh('mesh')
    .submit();

  assert.equal(result.status, ZLinkSubmitStatus.Submitted);
  assert.equal(submissions.length, 1);
  assert.deepEqual(submissions[0]?.target, {
    targetNodeRid: 'node-b',
    targetNodeGeneration: 7n,
    descriptorVersion: '11',
    targetSpotRid: 'instance-42',
    stableType: 'chat-room'
  });
  assert.equal(submissions[0]?.metadata?.get('trace'), 'abc');
  assert.ok((submissions[0]?.deadline ?? 0n) > BigInt(Date.now()));

  await outbound.sendToSpot('instance-absent-metadata', new Notice())
    .instanceSpot('chat-room')
    .inMesh('mesh')
    .submit();
  await outbound.sendToSpot('instance-empty-metadata', new Notice())
    .metadata(ZLinkMessageMetadataEmpty)
    .instanceSpot('chat-room')
    .inMesh('mesh')
    .submit();
  assert.equal(submissions[1]?.metadata, undefined);
  assert.equal(submissions[2]?.metadata?.size, 0);

  class Ping {}
  const reply = await outbound.requestToSpot('instance-43', new Ping())
    .instanceSpot('chat-room')
    .inMesh('mesh')
    .timeout(250)
    .submit<string>();
  assert.equal(reply, 'ready-reply');
  assert.equal(requested, true);
});

test('Missing Instance with zero types or no eligible descriptor uses exact target-not-found results', async () => {
  for (const mode of ['zeroTypes', 'noEligible'] as const) {
    const address = new ZLinkHostSpotAddressTransport({
      resolver: () => ({
        async resolve() {
          throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.SpotRouteNotFound,
            'missing'
          );
        }
      }),
      routed: {
        async sendToSpot() {
          throw new Error('Ready route must not be used.');
        },
        async requestToSpot() {
          throw new Error('Ready route must not be used.');
        }
      },
      meshNames: () => ['mesh'],
      isMeshConfigured: () => true,
      instanceTypes: () => mode === 'zeroTypes' ? [] : ['room'],
      meshNode: () => mode === 'zeroTypes'
        ? undefined
        : ({
            selectObjectPlacement() {
              return undefined;
            }
          } as never),
      completions: () => undefined,
      defaultRequestTimeoutMs: 100
    });
    const call = {
      instanceSpot: true,
      initialMeshName: 'mesh',
      metadata: new Map<string, string>()
    };
    assert.deepEqual(
      await address.sendToSpotAddress('missing-room', { hello: true }, call),
      { status: ZLinkSubmitStatus.TargetNotFound }
    );
    await assert.rejects(
      () => address.requestToSpotAddress('missing-room', { hello: true }, call),
      (error: unknown) => error instanceof ZLinkFrameworkException
        && error.kind === ZLinkFrameworkErrorKind.RequestTargetNotFound
    );
  }
});

test('Object Server role includes Object Client calling capability', () => {
  assert.equal(hasObjectClientCapability('none'), false);
  assert.equal(hasObjectClientCapability(undefined), false);
  assert.equal(hasObjectClientCapability('client'), true);
  assert.equal(hasObjectClientCapability('server'), true);
});

test('Ready one-way Spot send forwards application metadata through runtime route transport', async () => {
  const metadata = new Map([['trace', 'ready-send']]);
  let observed: ReadonlyMap<string, string> | undefined;
  const transport = new ZLinkRuntimeRouteTransport(() => ({
    async routeSendToSpot(
      _target: unknown,
      _packet: unknown,
      _message: unknown,
      _signal: unknown,
      forwarded: ReadonlyMap<string, string> | undefined
    ) {
      observed = forwarded;
    }
  } as never));
  const result = await transport.sendToSpot({
    routerChannelId: 'mesh',
    targetNodeRid: 'node-a',
    spotRid: 'ready-room',
    spotKind: 2 as never,
    stableType: 'room',
    targetSpotGeneration: 9n
  }, { hello: true }, { metadata });
  assert.equal(result.status, ZLinkSubmitStatus.Submitted);
  assert.equal(observed, metadata);
  assert.equal(observed?.get('trace'), 'ready-send');
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

function singleAuthorityStore(
  key: string,
  snapshot: ZLinkAuthoritySnapshot
): ZLinkAuthorityStore {
  const authorityKey = { value: key } as Parameters<ZLinkAuthorityStore['readAuthority']>[0];
  return {
    async readAuthority(requested: typeof authorityKey) {
      return requested.value === key
        ? snapshot
        : { kind: 'missing', storeNow: new Date() };
    },
    async listAuthorities() {
      return {
        kind: 'page',
        items: [{
          key: authorityKey,
          snapshot
        }]
      };
    }
  } as unknown as ZLinkAuthorityStore;
}

function instanceAuthoritySnapshot(options: {
  readonly spotRid: string;
  readonly meshName: string;
  readonly nodeRid: string;
  readonly storeVersion: string;
  readonly authorityOwnerGeneration: bigint;
  readonly state: 'coldActivating' | 'ready';
  readonly activationRecovery?: Parameters<
    typeof encodeServiceInstanceAuthorityPayload
  >[0]['activationRecovery'];
  readonly pendingCreation?: ZLinkAuthoritySnapshot['pendingCreation'];
}): ZLinkAuthoritySnapshot {
  const payload = encodeServiceInstanceAuthorityPayload({
    state: options.state,
    stableType: 'TenantWorker',
    spotRid: options.spotRid,
    ownerId: 'owner-a',
    ownerLeaseGeneration: 5n,
    ownerMeshName: options.meshName,
    ownerNodeRid: options.nodeRid,
    ownerNodeGeneration: 1n,
    ...(options.activationRecovery === undefined
      ? {}
      : { activationRecovery: options.activationRecovery })
  });
  if (options.state === 'ready') {
    assert.equal(decodeServiceReadySpotAuthority(payload)?.spotRid, options.spotRid);
  } else {
    assert.equal(decodeServiceReadySpotAuthority(payload), undefined);
  }
  return {
    kind: 'snapshot',
    storeVersion: { value: options.storeVersion } as ZLinkAuthoritySnapshot['storeVersion'],
    payload,
    objectGeneration: 11n,
    authorityOwnerGeneration: options.authorityOwnerGeneration,
    ownerId: 'owner-a',
    ownerLeaseGeneration: 5n,
    allocation: {
      // Active provider allocation alone is not the Framework Ready barrier.
      state: options.state === 'ready' ? 'active' : 'pending',
      objectKind: 'instance_spot',
      stableType: 'TenantWorker',
      descriptor: {
        meshName: options.meshName,
        rid: options.nodeRid
      },
      descriptorLifecycleGeneration: 1n,
      capacityDelta: 1
    },
    ...(options.pendingCreation === undefined
      ? {}
      : { pendingCreation: options.pendingCreation }),
    storeNow: new Date()
  };
}

class ReconcileAuthorityStore implements ZLinkAuthorityStore {
  readonly readKeys: string[] = [];
  scanExpired = false;
  private readonly rows: Array<[string, ZLinkAuthoritySnapshot]>;

  constructor(rows: Array<[string, ZLinkAuthoritySnapshot]>) {
    this.rows = rows;
  }

  replace(key: string, snapshot: ZLinkAuthoritySnapshot): void {
    const row = this.rows.find(entry => entry[0] === key);
    if (row === undefined) throw new Error(`Missing authority row '${key}'.`);
    row[1] = snapshot;
  }

  async readAuthority(key: ZLinkAuthorityKey) {
    this.readKeys.push(key.value);
    const snapshot = this.rows.find(entry => entry[0] === key.value)?.[1];
    return snapshot ?? { kind: 'missing' as const, storeNow: new Date() };
  }

  async compareExchangeAuthority(): Promise<never> {
    throw new Error('Not used by the reconciliation test.');
  }

  async listAuthorities(
    _prefix: string,
    cursor: ZLinkAuthorityScanCursor | undefined
  ) {
    if (this.scanExpired) return { kind: 'scanExpired' as const };
    const index = cursor === undefined ? 0 : 1;
    const row = this.rows[index];
    if (row === undefined) return { kind: 'page' as const, items: [] };
    return {
      kind: 'page' as const,
      items: [{
        key: { value: row[0] } as ZLinkAuthorityKey,
        // Deliberately stale: reconciliation must exact-read this key.
        snapshot: this.rows[0]![1]
      }],
      ...(index === 0
        ? { nextCursor: ZLinkAuthorityScanCursor.from('page-2') }
        : {})
    };
  }
}

class RecordingAuthorityNode {
  readonly recovered: Array<{
    readonly envelope: Parameters<
      ZLinkNodeRawMeshBackend['recoverInstanceActivation']
    >[0];
    readonly route: Parameters<
      ZLinkNodeRawMeshBackend['recoverInstanceActivation']
    >[1];
  }> = [];
  readonly recoveredPending: Array<{
    readonly envelope: Parameters<
      ZLinkNodeRawMeshBackend['recoverPendingInstanceActivation']
    >[0];
    readonly pending: Parameters<
      ZLinkNodeRawMeshBackend['recoverPendingInstanceActivation']
    >[1];
  }> = [];
  readonly remembered: Array<{
    readonly route: Parameters<ZLinkNodeRawMeshBackend['rememberSpotRoute']>[0];
    readonly storeVersion: string;
  }> = [];
  readonly forgotten: Array<{
    readonly spotRid: string;
    readonly authorityOwnerGeneration: bigint;
    readonly storeVersion: string;
  }> = [];
  readonly intents: Array<{
    readonly instanceType: string;
    readonly route: Parameters<ZLinkNodeRawMeshBackend['registerInstanceIntent']>[1];
  }> = [];
  readonly forgottenIntents: Array<{
    readonly spotRid: string;
    readonly authorityOwnerGeneration: bigint;
    readonly storeVersion: string;
  }> = [];

  constructor(
    private readonly meshName: string,
    private readonly nodeRid: string,
    private readonly completeRecovery: (
      target: Parameters<
        ZLinkNodeRawMeshBackend['completeRecoveredInstanceActivation']
      >[0],
      route: Parameters<
        ZLinkNodeRawMeshBackend['completeRecoveredInstanceActivation']
      >[1]
    ) => Promise<ServiceInstanceRouteFence> = async (_target, route) => route,
    private readonly descriptorRevision = 1n
  ) {}

  status() {
    return {
      meshName: this.meshName,
      routingId: this.nodeRid,
      lifecycleGeneration: 1n,
      descriptorRevision: this.descriptorRevision
    };
  }

  rememberSpotRoute(
    route: Parameters<ZLinkNodeRawMeshBackend['rememberSpotRoute']>[0],
    storeVersion: string
  ): void {
    this.remembered.push({ route, storeVersion });
  }

  forgetSpotRoute(
    spot: Parameters<ZLinkNodeRawMeshBackend['forgetSpotRoute']>[0],
    authorityOwnerGeneration: bigint,
    storeVersion: string
  ): void {
    this.forgotten.push({ spotRid: spot.spotRid, authorityOwnerGeneration, storeVersion });
  }

  registerInstanceIntent(
    instanceType: string,
    route: Parameters<ZLinkNodeRawMeshBackend['registerInstanceIntent']>[1]
  ): void {
    this.intents.push({ instanceType, route });
  }

  async recoverInstanceActivation(
    envelope: Parameters<ZLinkNodeRawMeshBackend['recoverInstanceActivation']>[0],
    route: Parameters<ZLinkNodeRawMeshBackend['recoverInstanceActivation']>[1]
  ): Promise<void> {
    this.recovered.push({ envelope, route });
  }

  async recoverPendingInstanceActivation(
    envelope: Parameters<ZLinkNodeRawMeshBackend['recoverPendingInstanceActivation']>[0],
    pending: Parameters<ZLinkNodeRawMeshBackend['recoverPendingInstanceActivation']>[1]
  ): Promise<void> {
    this.recoveredPending.push({ envelope, pending });
  }

  async completeRecoveredInstanceActivation(
    target: Parameters<
      ZLinkNodeRawMeshBackend['completeRecoveredInstanceActivation']
    >[0],
    route: Parameters<
      ZLinkNodeRawMeshBackend['completeRecoveredInstanceActivation']
    >[1]
  ): Promise<ServiceInstanceRouteFence> {
    return await this.completeRecovery(target, route);
  }

  forgetInstanceIntent(
    spotRid: string,
    authorityOwnerGeneration: bigint,
    storeVersion: string
  ): void {
    this.forgottenIntents.push({ spotRid, authorityOwnerGeneration, storeVersion });
  }
}
