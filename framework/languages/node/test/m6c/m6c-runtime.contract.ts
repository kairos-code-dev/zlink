import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { test } from 'node:test';
import {
  ServiceMaintenanceRuntime,
  classifyRelocationRecovery
} from '../../packages/framework/src/runtime/foundation/service-maintenance-runtime';
import {
  ServiceMailbox
} from '../../packages/framework/src/runtime/foundation/service-mailbox';
import {
  ZLinkInMemoryAuthorityStore
} from '../../packages/framework/src/runtime/locations/in-memory-authority-store';
import type {
  ZLinkAuthorityKey,
  ZLinkAuthoritySnapshot,
  ZLinkLocationOwnerToken,
  ZLinkObjectCreationTarget
} from '../../packages/framework/src/contracts';
import {
  ServiceDurableRelocationRuntime,
  ServiceRelocationAuthorityPayloadCodec,
  ServiceRelocationDataLostError,
  crc32c,
  decodeServiceRelocationEnvelope,
  encodeServiceRelocationEnvelope,
  type ServiceRelocationEnvelope,
  type ServiceRelocationQueuedMessage,
  type ServiceRelocationStorePort,
  type ServiceRelocationTimer
} from '../../packages/framework/src/runtime/foundation/service-relocation-runtime';
import {
  ServiceRelocationCoordinator,
  ServiceRelocationPostCommitError,
  ServiceRelocationSourceAuthorityWriter
} from '../../packages/framework/src/runtime/foundation/service-relocation-coordinator';
import {
  ServiceCapturedRelocationSourceCompletion,
  ServiceRelocationObjectCaptureOwner,
  ServiceRelocationObjectRestoreOwner,
  type ServiceObjectRelocationStaging,
  type ServiceRelocationCaptureUnit
} from '../../packages/framework/src/runtime/foundation/service-relocation-object-owner';
import {
  ZLinkManagedTimer
} from '../../packages/framework/src/runtime/spots/spot-timer';
import {
  ZLinkStatefulAuthorityRouteRuntime
} from '../../packages/framework/src/runtime/host/stateful-authority-route-runtime';
import { encodeAuthorityKey } from '../../packages/framework/src/runtime/locations/authority-key-codec';
import {
  ZLinkTimerOverrunPolicy
} from '../../packages/framework/src/contracts';
import {
  decodeServiceWireRelocationEnvelope,
  encodeServiceWireRelocationEnvelope
} from '../../packages/framework/src/runtime/foundation/service-relocation-wire-codec';
import {
  encodeServiceCanonicalRelocationAuthorityState,
  ServiceCanonicalRelocationPublicationRuntime
} from '../../packages/framework/src/runtime/foundation/service-relocation-authority-state';
import {
  decodeServiceReadySpotAuthority,
  encodeServiceUserSpotAuthorityPayload,
  replaceServiceAuthorityRelocationState
} from '../../packages/framework/src/runtime/foundation/service-authority-payload-codec';
import {
  decodeMaintenanceReplyRelay,
  decodeMaintenanceReplyRelayAck,
  decodeMaintenanceRelocationControl,
  decodeServiceWireFrozenRecord,
  encodeMaintenanceRelocationControl,
  encodeMaintenanceReplyRelay,
  encodeMaintenanceReplyRelayAck,
  encodeServiceWireFrozenRecord,
  type ServiceMaintenanceRelocationControl,
  type ServiceMaintenanceRelocationControlData
} from '../../packages/framework/src/runtime/foundation/service-stateful-wire-codec';

const relocationControlFixtureValues = (): readonly ServiceMaintenanceRelocationControl[] => {
  const base = {
    relocation: { high: 4n, low: 5n },
    targetAttemptGeneration: 6n,
    coordinator: {
      ownerId: 'coordinator', leaseGeneration: 7n, nodeRid: 'node-a',
      nodeGeneration: 11n, expectedAuthorityStoreVersion: 'store-3'
    }
  } as const;
  const candidate = {
    nodeRid: 'node-b', nodeGeneration: 12n,
    ownerId: 'target-owner', ownerLeaseGeneration: 8n
  } as const;
  const object = {
    kind: 'userSpot', spotId: 'spot-1', objectGeneration: 9n,
    expectedAuthorityOwnerGeneration: 10n
  } as const;
  const participants = [
    { participantId: 1n, allowanceMessages: 2n, allowanceBytes: 128n },
    { participantId: 2n, allowanceMessages: 0n, allowanceBytes: 0n }
  ] as const;
  const root = { reference: 'relocation-root', checksumCrc32c: 0x12345678 } as const;
  return [
    { kind: 'ready', ...base, round: 'initial', candidate, object, role: 'target',
      offeredMessages: 2n, offeredBytes: 128n, participants: [],
      sourceNodeGeneration: 11n, targetNodeGeneration: 12n, reservationGeneration: 13n,
      root, applicationVersion: 1n, participantProgress: [
        { participantId: 1n, acceptedBoundary: 2n, replayCursor: 0n },
        { participantId: 2n, acceptedBoundary: 0n, replayCursor: 0n }
      ] },
    { kind: 'data', ...base, senderRole: 'source', participantId: 1n, sequence: 1n,
      source: { ownerId: 'source-owner', leaseGeneration: 9n, nodeRid: 'node-a', nodeGeneration: 11n },
      object, phase: 'committed' },
    { kind: 'ack', ...base, senderRole: 'target', participantId: 1n, highWater: 2n },
    { kind: 'seal', ...base, senderRole: 'source', response: true,
      participants: [{ participantId: 1n, highWater: 2n }, { participantId: 2n, highWater: 0n }] },
    { kind: 'complete', ...base, senderRole: 'source',
      source: { ownerId: 'source-owner', leaseGeneration: 9n, nodeRid: 'node-a', nodeGeneration: 11n },
      sourceCleanupState: 'completed' },
    { kind: 'prepare', ...base, round: 'initial', candidate, initiatorRole: 'source', object,
      sourceNodeRid: 'node-a', sourceNodeGeneration: 11n,
      requiredMessages: 2n, requiredBytes: 128n, participants, root, applicationVersion: 1n },
    { kind: 'reserved', ...base, round: 'initial', candidate,
      reservationGeneration: 13n, participants }
  ];
};

function frozenU16(value: number): Buffer {
  const bytes = Buffer.alloc(2);
  bytes.writeUInt16BE(value);
  return bytes;
}

function frozenU32(value: number): Buffer {
  const bytes = Buffer.alloc(4);
  bytes.writeUInt32BE(value);
  return bytes;
}

function frozenU64(value: bigint): Buffer {
  const bytes = Buffer.alloc(8);
  bytes.writeBigUInt64BE(value);
  return bytes;
}

function frozenText8(value: string): Buffer {
  const bytes = Buffer.from(value);
  return Buffer.concat([Buffer.of(bytes.byteLength), bytes]);
}

function frozenText16(value: string): Buffer {
  const bytes = Buffer.from(value);
  return Buffer.concat([frozenU16(bytes.byteLength), bytes]);
}

function frozenBody16(value: Uint8Array): Buffer {
  return Buffer.concat([frozenU16(value.byteLength), value]);
}

function frozenPayload(): Buffer {
  const body = Buffer.concat([
    frozenText8('Packet'), frozenText8('application/json'), frozenU32(2), Buffer.of(1, 2)
  ]);
  return Buffer.concat([Buffer.of(1), frozenU32(body.byteLength), body]);
}

function frozenActorRef(actorId = 'actor'): Buffer {
  return Buffer.concat([frozenText8(actorId), frozenU64(3n)]);
}

function frozenSpotRef(spotId = 'spot'): Buffer {
  return Buffer.concat([frozenText8(spotId), frozenU64(4n)]);
}

function frozenActorRoute(): Buffer {
  return Buffer.concat([
    frozenActorRef(), frozenText8('node-t'), frozenU64(5n), frozenU64(6n), frozenU64(7n)
  ]);
}

function frozenSpotRoute(): Buffer {
  return Buffer.concat([
    frozenSpotRef(), frozenText8('node-t'), frozenU64(5n), frozenU64(6n), frozenU64(7n)
  ]);
}

function frozenSource(kind: number): Buffer {
  const fields = [
    frozenText8('node-s'), frozenU64(8n), frozenText8('owner-s'), frozenU64(9n)
  ];
  if (kind === 2) fields.push(frozenText8('spot-s'));
  else if (kind === 3 || kind === 4) {
    fields.push(frozenActorRef('actor-s'));
    if (kind === 4) {
      fields.push(frozenText8('session-s'), frozenU64(10n), frozenU64(11n));
    }
  }
  return Buffer.concat([Buffer.of(kind), frozenBody16(Buffer.concat(fields))]);
}

function frozenRecord(
  recordKind: number,
  sourceKind: number,
  operationKind: number,
  operationLow: bigint,
  replyRouteId: bigint | undefined,
  body: Uint8Array,
  metadata = false
): Buffer {
  const metadataFrame = metadata
    ? Buffer.concat([Buffer.of(1, 1, 1), frozenText8('trace'), frozenText16('abc')])
    : Buffer.of(0);
  const reply = replyRouteId === undefined ? Buffer.alloc(0) : frozenU64(replyRouteId);
  return Buffer.concat([
    Buffer.of(recordKind), frozenSource(sourceKind), metadataFrame,
    frozenU64(0n), frozenU64(operationLow), frozenU32(operationKind),
    frozenBody16(reply), body
  ]);
}

test('canonical commands 30..35 and 40..41 match the shared relocation-control fixture', () => {
  const fixture = JSON.parse(readFileSync(
    '../../runtime/protocol/golden/relocation-control-v1.json', 'utf8'
  )) as { readonly canonical: readonly { readonly command: number; readonly hex: string }[] };
  const values = relocationControlFixtureValues();
  assert.deepEqual(values.map(value => encodeMaintenanceRelocationControl(value).toString('hex')),
    fixture.canonical.map(value => value.hex));
  for (const [index, value] of values.entries()) {
    const encoded = Buffer.from(fixture.canonical[index]!.hex, 'hex');
    assert.equal(encoded[3], fixture.canonical[index]!.command);
    assert.deepEqual(decodeMaintenanceRelocationControl(encoded), value);
    assert.throws(() => decodeMaintenanceRelocationControl(encoded.subarray(0, -1)));
  }
  assert.throws(() => encodeMaintenanceRelocationControl({
    ...values[5] as Extract<ServiceMaintenanceRelocationControl, { kind: 'prepare' }>,
    participants: [
      { participantId: 1n, allowanceMessages: 0n, allowanceBytes: 0n },
      { participantId: 1n, allowanceMessages: 0n, allowanceBytes: 0n }
    ]
  }), /sorted and unique/);
});

test('canonical relocation data preserves every exact relocation phase and rejects unknown phase 10', () => {
  const base = relocationControlFixtureValues()[1] as ServiceMaintenanceRelocationControlData;
  const phases = ['none', 'preparing', 'captured', 'prepared', 'committed',
    'activating', 'activated', 'cleaning', 'completed', 'aborted'] as const;
  for (const phase of phases) {
    const value = { ...base, phase };
    assert.deepEqual(decodeMaintenanceRelocationControl(
      encodeMaintenanceRelocationControl(value)
    ), value);
  }

  const none = encodeMaintenanceRelocationControl({ ...base, phase: 'none' });
  const aborted = encodeMaintenanceRelocationControl({ ...base, phase: 'aborted' });
  const differingOffsets = [...none.keys()].filter(index => none[index] !== aborted[index]);
  assert.equal(differingOffsets.length, 1);
  const unknown = Buffer.from(none);
  unknown[differingOffsets[0]!] = 10;
  assert.throws(() => decodeMaintenanceRelocationControl(unknown), /control phase/);
});

test('canonical frozen-record closed union validates all 14 bodies and bound-session request identity', () => {
  const payload = frozenPayload();
  const channelBody = Buffer.concat([frozenText8('channel'), payload]);
  const spotBody = Buffer.concat([frozenSpotRoute(), payload]);
  const actorBody = Buffer.concat([frozenActorRoute(), payload]);
  const snapshot = Buffer.concat([frozenActorRef(), frozenSpotRef()]);
  const instanceRoute = Buffer.concat([
    frozenText8('node-t'), frozenU64(5n), frozenText8('spot-1'), frozenText8('mesh'),
    frozenText8('instance-type'), frozenText8('descriptor-v1'), frozenU64(1000n)
  ]);
  const records = [
    frozenRecord(1, 1, 0, 0n, undefined, payload, true),
    frozenRecord(2, 4, 1, 1n, 12n, payload, true),
    frozenRecord(3, 2, 0, 0n, undefined, channelBody, true),
    frozenRecord(4, 3, 2, 2n, 13n, channelBody, true),
    frozenRecord(5, 1, 0, 3n, undefined, spotBody, true),
    frozenRecord(6, 4, 3, 4n, 14n, spotBody, true),
    frozenRecord(7, 2, 0, 0n, undefined,
      Buffer.concat([frozenText8('channel'), frozenText8('topic'), payload]), true),
    frozenRecord(8, 1, 7, 5n, undefined,
      Buffer.concat([Buffer.of(1), frozenBody16(snapshot)])),
    frozenRecord(9, 3, 0, 6n, undefined, actorBody, true),
    frozenRecord(10, 4, 4, 7n, 15n, actorBody, true),
    frozenRecord(11, 3, 4, 8n, 16n,
      Buffer.concat([frozenU32(0), frozenU32(0), Buffer.of(1), payload])),
    frozenRecord(12, 1, 0, 0n, undefined,
      Buffer.concat([Buffer.of(1), frozenBody16(frozenText8('node-t'))])),
    frozenRecord(13, 1, 0, 0n, undefined, Buffer.concat([
      Buffer.of(4, 1), frozenU64(4n), frozenU64(5n), Buffer.of(2),
      frozenBody16(Buffer.concat([frozenText8('spot-1'), frozenU64(9n), frozenU64(10n)])),
      frozenU32(0), frozenU32(0)
    ])),
    frozenRecord(14, 2, 12, 9n, 17n, Buffer.concat([
      Buffer.of(2), frozenBody16(instanceRoute), frozenU64(8n), Buffer.of(2), payload
    ]), true)
  ];
  for (const [index, record] of records.entries()) {
    const decoded = decodeServiceWireFrozenRecord(record);
    assert.equal(decoded.recordKind, index + 1);
    assert.deepEqual(encodeServiceWireFrozenRecord(decoded), record);
    assert.throws(() => decodeServiceWireFrozenRecord(record.subarray(0, -1)));
  }
  const boundRequest = decodeServiceWireFrozenRecord(records[1]!);
  assert.equal(boundRequest.sourceKind, 4);
  assert.deepEqual(boundRequest.sourceActor, { actorId: 'actor-s', generation: 3n });
  assert.equal(boundRequest.sourceSessionRid, 'session-s');
  assert.equal(boundRequest.sourceBindingGeneration, 10n);
  assert.equal(boundRequest.sourceSessionSequence, 11n);
  assert.equal(boundRequest.replyRouteId, 12n);
  assert.throws(() => encodeServiceWireFrozenRecord({ ...boundRequest, operationKind: 2 }));

  const control = relocationControlFixtureValues()[1] as ServiceMaintenanceRelocationControlData;
  const encodedData = encodeMaintenanceRelocationControl({
    kind: 'data', relocation: control.relocation,
    targetAttemptGeneration: control.targetAttemptGeneration, coordinator: control.coordinator,
    senderRole: control.senderRole, participantId: control.participantId,
    sequence: control.sequence, frozenRecord: boundRequest
  });
  const decodedData = decodeMaintenanceRelocationControl(encodedData);
  assert.equal(decodedData.kind, 'data');
  if (decodedData.kind === 'data' && decodedData.frozenRecord !== undefined) {
    assert.deepEqual(decodedData.frozenRecord.canonicalBytes, records[1]);
  } else {
    assert.fail('Command 31 did not preserve its canonical frozen record.');
  }
});

test('canonical relocation golden decodes progress and completion and re-encodes byte-identically', () => {
  const fixture = JSON.parse(readFileSync(
    '../../runtime/protocol/golden/relocation-envelope-v1.json',
    'utf8'
  )) as { readonly logicalHex: string };
  const encoded = Buffer.from(fixture.logicalHex, 'hex');
  const envelope = decodeServiceWireRelocationEnvelope(encoded);
  assert.deepEqual(envelope.participantProgress, [
    { participantId: 1n, acceptedBoundary: 2n, replayCursor: 0n },
    { participantId: 2n, acceptedBoundary: 0n, replayCursor: 0n }
  ]);
  assert.equal(envelope.terminalCompletions.length, 1);
  assert.deepEqual(envelope.terminalCompletions[0], {
    operationHigh: 0n,
    operationLow: 42n,
    sourceOwnerId: 'request-source',
    sourceOwnerLeaseGeneration: 6n,
    sourceNodeRid: 'n',
    sourceNodeGeneration: 1n,
    participantId: 1n,
    sequence: 1n,
    terminalResult: 0,
    failureCode: 0,
    deliveryState: 1,
    payload: {
      packetName: 'ChatReply',
      contentType: 'application/json',
      bytes: Buffer.from('{"ok":true}')
    }
  });
  assert.deepEqual(encodeServiceWireRelocationEnvelope(envelope), encoded);

  const successorBytes = encodeServiceWireRelocationEnvelope({
    ...envelope,
    participantProgress: envelope.participantProgress.map(progress =>
      progress.participantId === 1n ? { ...progress, replayCursor: 1n } : progress),
    terminalCompletions: envelope.terminalCompletions.map(completion => ({
      ...completion,
      deliveryState: 0
    }))
  });
  assert.notDeepEqual(successorBytes, encoded);
  const successor = decodeServiceWireRelocationEnvelope(successorBytes);
  assert.equal(successor.participantProgress[0]?.replayCursor, 1n);
  assert.equal(successor.terminalCompletions[0]?.deliveryState, 0);
});

test('canonical command 33 and 46 match the shared reply-relay byte fixture', () => {
  const fixture = JSON.parse(readFileSync(
    '../../runtime/protocol/golden/reply-relay-v1.json',
    'utf8'
  )) as { readonly canonical: readonly { readonly name: string; readonly hex: string }[] };
  const command33 = Buffer.from(
    fixture.canonical.find(value => value.name === 'maintenanceReplyRelay')!.hex,
    'hex'
  );
  const command46 = Buffer.from(
    fixture.canonical.find(value => value.name === 'replyRelayAlreadyTerminalAck')!.hex,
    'hex'
  );
  const coordinator = {
    ownerId: 'coordinator',
    leaseGeneration: 7n,
    nodeRid: 'node-a',
    nodeGeneration: 11n,
    expectedAuthorityStoreVersion: 'store-3'
  };
  const relay = {
    relocation: { high: 4n, low: 5n },
    targetAttemptGeneration: 6n,
    coordinator,
    operation: { high: 1n, low: 2n },
    replyRouteId: 3n,
    participantId: 8n,
    sequence: 9n,
    terminalResult: 101,
    failureCode: 0
  };
  assert.deepEqual(encodeMaintenanceReplyRelay(relay), command33);
  assert.deepEqual(decodeMaintenanceReplyRelay(command33), relay);
  assert.throws(() => encodeMaintenanceReplyRelay({
    ...relay,
    terminalResult: 105,
    failureCode: 1
  }), /does not match/);
  assert.throws(() => encodeMaintenanceReplyRelay({
    ...relay,
    payload: {
      packetName: 'Reply',
      contentType: 'application/json',
      bytes: Buffer.from('{}')
    }
  }), /no payload/);

  const ack = {
    relocation: { high: 4n, low: 5n },
    coordinator,
    operation: { high: 1n, low: 2n },
    requestSource: {
      ownerId: 'source',
      leaseGeneration: 13n,
      nodeRid: 'node-s',
      nodeGeneration: 17n
    },
    status: 'alreadyTerminal' as const
  };
  assert.deepEqual(encodeMaintenanceReplyRelayAck(ack), command46);
  assert.deepEqual(decodeMaintenanceReplyRelayAck(command46), ack);
  assert.throws(() => decodeMaintenanceReplyRelay(command33.subarray(0, -1)));
  assert.throws(() => decodeMaintenanceReplyRelayAck(Buffer.concat([command46, Buffer.of(0)])));
});

test('canonical relocation rejects bounds, progress order, replay overflow, and trailing bytes', () => {
  const fixture = JSON.parse(readFileSync(
    '../../runtime/protocol/golden/relocation-envelope-v1.json',
    'utf8'
  )) as { readonly logicalHex: string };
  const encoded = Buffer.from(fixture.logicalHex, 'hex');
  const tooManyStates = Buffer.from(encoded);
  tooManyStates.writeUInt32BE(1_025, 48);
  assert.throws(
    () => decodeServiceWireRelocationEnvelope(tooManyStates),
    /count exceeds its bound/
  );

  const progressPrefix = Buffer.from(
    '00000002000000000000000100000000000000020000000000000000',
    'hex'
  );
  const progressOffset = encoded.indexOf(progressPrefix);
  assert.ok(progressOffset > 0);
  const replayOverflow = Buffer.from(encoded);
  replayOverflow[progressOffset + progressPrefix.length - 1] = 3;
  assert.throws(
    () => decodeServiceWireRelocationEnvelope(replayOverflow),
    /participant progress/
  );

  const duplicateProgress = Buffer.from(encoded);
  duplicateProgress.writeBigUInt64BE(1n, progressOffset + 4 + 24);
  assert.throws(
    () => decodeServiceWireRelocationEnvelope(duplicateProgress),
    /participant progress/
  );
  assert.throws(
    () => decodeServiceWireRelocationEnvelope(Buffer.concat([encoded, Buffer.from([0])])),
    /trailing bytes/
  );
});

test('canonical authority relocation state derives exact root counts and fails before CAS without fences', () => {
  const fixture = JSON.parse(readFileSync(
    '../../runtime/protocol/golden/relocation-envelope-v1.json',
    'utf8'
  )) as { readonly logicalHex: string };
  const root = decodeServiceWireRelocationEnvelope(Buffer.from(fixture.logicalHex, 'hex'));
  const state = {
    relocationHigh: root.relocationHigh,
    relocationLow: root.relocationLow,
    targetAttemptGeneration: 1n,
    sourceNodeRid: 'node-a',
    sourceNodeGeneration: 1n,
    sourceOwnerId: 'owner-a',
    sourceOwnerLeaseGeneration: 1n,
    targetNodeRid: 'node-b',
    targetNodeGeneration: 2n,
    targetOwnerId: 'owner-b',
    targetOwnerLeaseGeneration: 2n,
    reservationGeneration: 1n,
    coordinatorOwnerId: 'owner-a',
    coordinatorLeaseGeneration: 1n,
    coordinatorNodeRid: 'node-a',
    coordinatorNodeGeneration: 1n,
    phase: 3 as const,
    relocationReference: 'root:canonical',
    relocationChecksumCrc32c: 42,
    applicationVersion: root.applicationVersion,
    sourceCleanupState: 0 as const
  };
  const encodedState = encodeServiceCanonicalRelocationAuthorityState(state, root);
  const steady = encodeServiceUserSpotAuthorityPayload({
    state: 'ready', stableType: 'room', spotId: 'room', ownerId: 'owner-a',
    ownerLeaseGeneration: 1n, ownerMeshName: 'mesh', ownerNodeRid: 'node-a',
    ownerNodeGeneration: 1n
  });
  const relocated = replaceServiceAuthorityRelocationState(steady, encodedState);
  assert.equal(decodeServiceReadySpotAuthority(relocated), undefined);
  assert.throws(() => encodeServiceCanonicalRelocationAuthorityState({
    ...state,
    coordinatorNodeGeneration: 0n
  }, root), /fence is unavailable/);
  assert.throws(() => encodeServiceCanonicalRelocationAuthorityState({
    ...state,
    relocationLow: root.relocationLow + 1n
  }, root), /identity differs/);
});

test('canonical authority CAS reconciles response loss and retains pending relay root until ACK', async () => {
  const fixture = JSON.parse(readFileSync(
    '../../runtime/protocol/golden/relocation-envelope-v1.json', 'utf8'
  )) as { readonly logicalHex: string };
  const golden = decodeServiceWireRelocationEnvelope(Buffer.from(fixture.logicalHex, 'hex'));
  const pending = decodeServiceWireRelocationEnvelope(encodeServiceWireRelocationEnvelope({
    ...golden,
    terminalCompletions: golden.terminalCompletions.map(value => ({ ...value, deliveryState: 0 }))
  }));
  const authority = authorityStore();
  const key = authorityKey('canonical-room');
  const created = await createAuthority(authority, 'canonical-room');
  const steadyPayload = encodeServiceUserSpotAuthorityPayload({
    state: 'ready', stableType: 'room', spotId: 'canonical-room', ownerId: 'owner-a',
    ownerLeaseGeneration: 1n, ownerMeshName: 'mesh', ownerNodeRid: 'node-a',
    ownerNodeGeneration: 1n
  });
  const steadyResult = await authority.compareExchangeAuthority(key, created.storeVersion, {
    kind: 'put', generationTransition: 'preserve', payload: steadyPayload
  });
  assert.equal(steadyResult.kind, 'stored');
  if (steadyResult.kind !== 'stored') return;
  const { kind: _kind, ...steadySnapshot } = steadyResult;
  const steady: ZLinkAuthoritySnapshot = { kind: 'snapshot', ...steadySnapshot };
  let loseResponse = true;
  const runtime = new ServiceCanonicalRelocationPublicationRuntime({
    readAuthority: (...args) => authority.readAuthority(...args),
    async compareExchangeAuthority(...args) {
      const result = await authority.compareExchangeAuthority(...args);
      if (loseResponse) {
        loseResponse = false;
        throw new Error('CAS response lost');
      }
      return result;
    }
  }, new MemoryRelocationStore([]));
  const state = {
    relocationHigh: pending.relocationHigh, relocationLow: pending.relocationLow,
    targetAttemptGeneration: 1n, sourceNodeRid: 'node-a', sourceNodeGeneration: 1n,
    sourceOwnerId: 'owner-a', sourceOwnerLeaseGeneration: 1n,
    targetNodeRid: 'node-b', targetNodeGeneration: 2n, targetOwnerId: 'owner-b',
    targetOwnerLeaseGeneration: 2n, reservationGeneration: 1n,
    coordinatorOwnerId: 'owner-a', coordinatorLeaseGeneration: 1n,
    coordinatorNodeRid: 'node-a', coordinatorNodeGeneration: 1n,
    phase: 3 as const, applicationVersion: pending.applicationVersion,
    sourceCleanupState: 0 as const
  };
  const published = await runtime.publish(key, steady, pending, state);
  await assert.rejects(runtime.release(key, published), /pending reply relays/);
  const acknowledged = decodeServiceWireRelocationEnvelope(encodeServiceWireRelocationEnvelope({
    ...pending,
    terminalCompletions: pending.terminalCompletions.map(value => ({ ...value, deliveryState: 1 }))
  }));
  const completed = await runtime.replace(key, published, acknowledged, {
    ...state, phase: 8, sourceCleanupState: 1
  });
  const released = await runtime.release(key, completed);
  assert.ok(decodeServiceReadySpotAuthority(released.payload));
});

test('Preparing recovery restores steady authority after the stored owner lease expires', async () => {
  let ownerLive = true;
  const authority = new ZLinkInMemoryAuthorityStore({
    isTargetLive: () => ownerLive
  }, () => new Date(100));
  const key = authorityKey('preparing-recovery');
  const ready = await createAuthority(authority, 'preparing-recovery');
  const codec = new ServiceRelocationAuthorityPayloadCodec();
  const preparing = await authority.compareExchangeAuthority(
    key,
    ready.storeVersion,
    {
      kind: 'put',
      generationTransition: 'preserve',
      payload: codec.prepare(ready.payload)
    }
  );
  assert.equal(preparing.kind, 'stored');
  if (preparing.kind !== 'stored') return;

  ownerLive = false;
  const normalWrite = await authority.compareExchangeAuthority(
    key,
    preparing.storeVersion,
    { kind: 'put', generationTransition: 'preserve', payload: ready.payload }
  );
  assert.equal(normalWrite.kind, 'conflict');

  const restored = await authority.compareExchangeAuthority(
    key,
    preparing.storeVersion,
    {
      kind: 'restore',
      payload: codec.readPreparing(preparing.payload)!,
      expectedOwner: owner('owner-a', 1n)
    }
  );
  assert.equal(restored.kind, 'stored');
  if (restored.kind !== 'stored') return;
  assert.equal(Buffer.from(restored.payload).toString(), 'owner-state');

  const wrongOwner = await authority.compareExchangeAuthority(
    key,
    restored.storeVersion,
    {
      kind: 'restore',
      payload: Buffer.from('wrong'),
      expectedOwner: owner('owner-b', 2n)
    }
  );
  assert.equal(wrongOwner.kind, 'conflict');
});

test('startup recovery removes a rootless Preparing marker before route admission', async () => {
  let ownerLive = true;
  const authority = new ZLinkInMemoryAuthorityStore({
    isTargetLive: () => ownerLive
  }, () => new Date(100));
  const key = authorityKey('startup-preparing-recovery');
  const ready = await createAuthority(authority, 'startup-preparing-recovery');
  const codec = new ServiceRelocationAuthorityPayloadCodec();
  const preparing = await authority.compareExchangeAuthority(
    key,
    ready.storeVersion,
    {
      kind: 'put',
      generationTransition: 'preserve',
      payload: codec.prepare(ready.payload)
    }
  );
  assert.equal(preparing.kind, 'stored');

  ownerLive = false;
  const runtime = new ZLinkStatefulAuthorityRouteRuntime({
    store: authority,
    meshNodes: new Map(),
    pollingIntervalMs: 60_000,
    pageSize: 32,
    reportError: error => { throw error; }
  });
  await runtime.start();
  await runtime.stop();

  const recovered = await authority.readAuthority(key);
  assert.equal(recovered.kind, 'snapshot');
  if (recovered.kind !== 'snapshot') return;
  assert.equal(codec.readPreparing(recovered.payload), undefined);
  assert.equal(Buffer.from(recovered.payload).toString(), 'owner-state');
});

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
    decoded.participants.map(participant => [
      participant.objectKind,
      participant.stableType,
      participant.objectGeneration,
      participant.authorityOwnerGeneration
    ]),
    [
      ['actor', 'player', 8n, 5n],
      ['user_spot', 'room', 3n, 2n]
    ]
  );
  const spot = decoded.participants[1]!;
  assert.deepEqual(
    spot.queuedMessages.map(({ sequence }) => sequence),
    [1n, 2n]
  );
  assert.deepEqual(
    spot.timers.map(({ timerId }) => timerId),
    ['heartbeat', 'idle']
  );
  assert.deepEqual(
    encodeServiceRelocationEnvelope(decoded),
    encoded
  );
});

test('relocation envelope rejects unknown identity and participant work fields', () => {
  const encoded = encodeServiceRelocationEnvelope(relocationEnvelope());
  const parsed = JSON.parse(encoded.toString('utf8')) as {
    version: number;
    participants: Array<Record<string, unknown>>;
  };

  parsed.participants[0]!.objectGeneration = '0';
  assert.throws(
    () => decodeServiceRelocationEnvelope(Buffer.from(JSON.stringify(parsed))),
    /object generation must be a positive integer/
  );

  const unknownKind = JSON.parse(encoded.toString('utf8')) as {
    participants: Array<Record<string, unknown>>;
  };
  unknownKind.participants[0]!.objectKind = 'channel';
  assert.throws(
    () => decodeServiceRelocationEnvelope(Buffer.from(JSON.stringify(unknownKind))),
    /object kind is invalid/
  );

  const unknownField = JSON.parse(encoded.toString('utf8')) as Record<string, unknown>;
  unknownField.legacyQueue = [];
  assert.throws(
    () => decodeServiceRelocationEnvelope(Buffer.from(JSON.stringify(unknownField))),
    /envelope fields/
  );
});

test('relocation envelope rejects duplicate participant queue and timer identities', () => {
  const envelope = relocationEnvelope();
  const spot = envelope.participants[0]!;
  const queueEnvelope: ServiceRelocationEnvelope = {
    ...envelope,
    participants: [{
      ...spot,
      queuedMessages: [spot.queuedMessages[0]!, spot.queuedMessages[0]!]
    }]
  };
  assert.throws(
    () => encodeServiceRelocationEnvelope(queueEnvelope),
    /queue sequences must be unique per participant/
  );

  const timerEnvelope: ServiceRelocationEnvelope = {
    ...envelope,
    participants: [{
      ...spot,
      timers: [spot.timers[0]!, spot.timers[0]!]
    }]
  };
  assert.throws(
    () => encodeServiceRelocationEnvelope(timerEnvelope),
    /timer ids must be unique per participant/
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
  const authority = authorityStore();
  const key = authorityKey('spot:room');
  const initial = await createAuthority(authority, 'spot:room');
  const authorityPort = {
    readAuthority: (...args: Parameters<ZLinkInMemoryAuthorityStore['readAuthority']>) =>
      authority.readAuthority(...args),
    compareExchangeAuthority: (
      ...args: Parameters<ZLinkInMemoryAuthorityStore['compareExchangeAuthority']>
    ) => {
      events.push('authority-cas');
      return authority.compareExchangeAuthority(...args);
    }
  };
  const store = new MemoryRelocationStore(events);
  const runtime = new ServiceDurableRelocationRuntime(authorityPort, store, authorityCodec);
  const published = await runtime.captureAndPublish(
    key,
    initial,
    owner('owner-b', 2n),
    relocationEnvelope()
  );
  assert.deepEqual(events.slice(0, 3), ['authority-cas', 'payload-put', 'authority-cas']);
  assert.equal(published.authority.objectGeneration, initial.objectGeneration);
  assert.equal(
    published.authority.authorityOwnerGeneration,
    initial.authorityOwnerGeneration
  );
  const restored = await runtime.restore(published.authority);
  const restoredSpot = restored.participants.find(({ objectKind }) =>
    objectKind === 'user_spot'
  );
  assert.ok(restoredSpot);
  assert.deepEqual(
    restoredSpot.queuedMessages.map(({ sequence }) => sequence),
    [1n, 2n]
  );
  assert.equal(restoredSpot.timers[0]?.pendingTicks, 1);

  events.length = 0;
  const released = await runtime.release(key, published.authority);
  assert.deepEqual(events, ['authority-cas', 'payload-delete']);
  assert.equal(authorityCodec.read(released.payload), undefined);
});

test('failed publication removes only the orphan and published data loss never rolls back', async () => {
  const events: string[] = [];
  const authority = authorityStore();
  const key = authorityKey('actor:a');
  const initial = await createAuthority(authority, 'actor:a');
  let authorityCasCount = 0;
  const authorityPort = {
    readAuthority: (...args: Parameters<ZLinkInMemoryAuthorityStore['readAuthority']>) =>
      authority.readAuthority(...args),
    compareExchangeAuthority: async (
      ...args: Parameters<ZLinkInMemoryAuthorityStore['compareExchangeAuthority']>
    ) => {
      authorityCasCount += 1;
      if (authorityCasCount === 2) {
        return {
          kind: 'conflict' as const,
          current: await authority.readAuthority(key)
        };
      }
      return authority.compareExchangeAuthority(...args);
    }
  };
  const store = new MemoryRelocationStore(events);
  const runtime = new ServiceDurableRelocationRuntime(authorityPort, store, authorityCodec);
  await assert.rejects(
    runtime.captureAndPublish(
      key,
      initial,
      owner('owner-b', 2n),
      relocationEnvelope()
    ),
    /rejected relocation publication/
  );
  assert.deepEqual(events, ['payload-put', 'payload-delete']);

  const current = await authority.readAuthority(key);
  assert.equal(current.kind, 'snapshot');
  if (current.kind !== 'snapshot') return;
  authorityCasCount = 0;
  const published = await new ServiceDurableRelocationRuntime(
    authority,
    store,
    authorityCodec
  ).captureAndPublish(
    key,
    current,
    owner('owner-b', 2n),
    relocationEnvelope()
  );
  await store.delete(published.publication.reference);
  await assert.rejects(
    runtime.restore(published.authority),
    ServiceRelocationDataLostError
  );
  const afterLoss = await authority.readAuthority(key);
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
  const authority = authorityStore();
  const key = authorityKey('spot:response-loss');
  const initial = await createAuthority(authority, 'spot:response-loss');
  const authorityPort = {
    readAuthority: (...args: Parameters<ZLinkInMemoryAuthorityStore['readAuthority']>) =>
      authority.readAuthority(...args),
    compareExchangeAuthority: async (
      ...args: Parameters<ZLinkInMemoryAuthorityStore['compareExchangeAuthority']>
    ) => {
      events.push('authority-cas');
      const committed = await authority.compareExchangeAuthority(...args);
      assert.equal(committed.kind, 'stored');
      throw new Error('authority response lost');
    }
  };
  const store = new MemoryRelocationStore(events);
  const runtime = new ServiceDurableRelocationRuntime(authorityPort, store, authorityCodec);

  const published = await runtime.captureAndPublish(
    key,
    initial,
    owner('owner-b', 2n),
    relocationEnvelope()
  );

  assert.deepEqual(events, ['authority-cas', 'payload-put', 'authority-cas']);
  assert.equal(
    authorityCodec.read(published.authority.payload)?.reference,
    published.publication.reference
  );
  assert.equal((await store.get(published.publication.reference)).kind, 'found');
});

test('source cleanup publishes completed immutable root before deleting pending root', async () => {
  const events: string[] = [];
  const authority = authorityStore();
  const key = authorityKey('spot:source-completion');
  const initial = await createAuthority(authority, 'spot:source-completion');
  const authorityPort = {
    readAuthority: (...args: Parameters<ZLinkInMemoryAuthorityStore['readAuthority']>) =>
      authority.readAuthority(...args),
    compareExchangeAuthority: (
      ...args: Parameters<ZLinkInMemoryAuthorityStore['compareExchangeAuthority']>
    ) => {
      events.push('authority-cas');
      return authority.compareExchangeAuthority(...args);
    }
  };
  const store = new MemoryRelocationStore(events);
  const durable = new ServiceDurableRelocationRuntime(authorityPort, store, authorityCodec);
  const published = await durable.captureAndPublish(
    key,
    initial,
    owner('owner-b', 2n),
    relocationEnvelope()
  );
  const fence = await reserveTarget(authority, key, published.authority, 'source-completion');
  const committed = await durable.commitOwner(
    key,
    published.authority,
    owner('owner-b', 2n),
    fence
  );
  const writer = new ServiceRelocationSourceAuthorityWriter(
    durable,
    (staging: { readonly id: string; readonly key: ZLinkAuthorityKey }) => staging.key
  );

  events.length = 0;
  const completed = await writer.complete(
    { id: 'source-completion', key },
    committed
  );
  assert.deepEqual(events, ['payload-put', 'authority-cas', 'payload-delete']);
  const completion = authorityCodec.read(completed.payload);
  assert.ok(completion);
  assert.equal(completion.phase, 'sourceCleanupCompleted');
  assert.equal(
    completion.aggregateGeneration,
    published.publication.aggregateGeneration + 1n
  );
  assert.equal(completion.aggregateId, published.publication.aggregateId);
  assert.equal(completion.targetOwnerId, 'owner-b');
  assert.equal(completion.targetOwnerLeaseGeneration, 2n);
  assert.equal(completion.inventoryDigest, published.publication.inventoryDigest);
  assert.equal(completed.objectGeneration, committed.objectGeneration);
  assert.equal(
    completed.authorityOwnerGeneration,
    committed.authorityOwnerGeneration
  );
  assert.equal((await store.get(published.publication.reference)).kind, 'missing');
  assert.equal((await store.get(completion.reference)).kind, 'found');
  const restored = await durable.restore(completed);
  assert.equal(restored.sourceCleanup, 'completed');
  assert.equal(restored.aggregateGeneration, completion.aggregateGeneration);
});

test('terminal relay acknowledgement advances the completed root before its recovery pointer is released', async () => {
  const events: string[] = [];
  const authority = authorityStore();
  const key = authorityKey('spot:terminal-delivery');
  const initial = await createAuthority(authority, 'spot:terminal-delivery');
  const store = new MemoryRelocationStore(events);
  const durable = new ServiceDurableRelocationRuntime(authority, store, authorityCodec);
  const published = await durable.captureAndPublish(
    key,
    initial,
    owner('owner-b', 2n),
    relocationEnvelope()
  );
  const fence = await reserveTarget(authority, key, published.authority, 'terminal-delivery');
  const committed = await durable.commitOwner(
    key,
    published.authority,
    owner('owner-b', 2n),
    fence
  );
  const pendingTerminal = Buffer.from('terminal:pending');
  const writer = new ServiceRelocationSourceAuthorityWriter(
    durable,
    (staging: { readonly id: string; readonly key: ZLinkAuthorityKey }) => staging.key,
    () => new Map([[
      'actor:a',
      { replayCursor: 1n, terminalReplies: pendingTerminal, pendingRelayCount: 1 }
    ]])
  );
  const completed = await writer.complete(
    { id: 'terminal-delivery', key },
    committed
  );
  const completedPublication = authorityCodec.read(completed.payload);
  assert.ok(completedPublication);
  const beforeAck = await durable.restore(completed);
  const beforeAckActor = beforeAck.participants.find(participant => participant.key === 'actor:a');
  assert.equal(beforeAckActor?.pendingRelayCount, 1);
  assert.deepEqual(beforeAckActor?.terminalReplies, pendingTerminal);

  const deliveredTerminal = Buffer.from('terminal:alreadyTerminal');
  const acknowledged = await durable.advanceCompletedProgress(
    key,
    completed,
    new Map([[
      'actor:a',
      { replayCursor: 1n, terminalReplies: deliveredTerminal, pendingRelayCount: 0 }
    ]]),
    undefined,
    { retainPreviousRoot: true }
  );
  const acknowledgedPublication = authorityCodec.read(acknowledged.payload);
  assert.ok(acknowledgedPublication);
  assert.equal(
    acknowledgedPublication.aggregateGeneration,
    completedPublication.aggregateGeneration + 1n
  );
  const afterAck = await durable.restore(acknowledged);
  const afterAckActor = afterAck.participants.find(participant => participant.key === 'actor:a');
  assert.equal(afterAckActor?.pendingRelayCount, 0);
  assert.deepEqual(afterAckActor?.terminalReplies, deliveredTerminal);
  assert.equal((await store.get(completedPublication.reference)).kind, 'found');
  assert.equal((await store.get(acknowledgedPublication.reference)).kind, 'found');

  assert.equal(
    await durable.deleteRetainedRoot(completedPublication.reference),
    'deleted'
  );
});

test('source cleanup response loss reconciles completed authority and stale retry is idempotent', async () => {
  const events: string[] = [];
  const authority = authorityStore();
  const key = authorityKey('spot:source-response-loss');
  const initial = await createAuthority(authority, 'spot:source-response-loss');
  let loseResponse = false;
  const authorityPort = {
    readAuthority: (...args: Parameters<ZLinkInMemoryAuthorityStore['readAuthority']>) =>
      authority.readAuthority(...args),
    compareExchangeAuthority: async (
      ...args: Parameters<ZLinkInMemoryAuthorityStore['compareExchangeAuthority']>
    ) => {
      events.push('authority-cas');
      const result = await authority.compareExchangeAuthority(...args);
      if (loseResponse) {
        loseResponse = false;
        throw new Error('source cleanup authority response lost');
      }
      return result;
    }
  };
  const store = new MemoryRelocationStore(events);
  const durable = new ServiceDurableRelocationRuntime(authorityPort, store, authorityCodec);
  const published = await durable.captureAndPublish(
    key,
    initial,
    owner('owner-b', 2n),
    relocationEnvelope()
  );
  const fence = await reserveTarget(authority, key, published.authority, 'source-response-loss');
  const committed = await durable.commitOwner(
    key,
    published.authority,
    owner('owner-b', 2n),
    fence
  );
  const writer = new ServiceRelocationSourceAuthorityWriter(
    durable,
    (staging: { readonly id: string; readonly key: ZLinkAuthorityKey }) => staging.key
  );

  events.length = 0;
  loseResponse = true;
  const completed = await writer.complete(
    { id: 'source-response-loss', key },
    committed
  );
  assert.deepEqual(events, ['payload-put', 'authority-cas', 'payload-delete']);
  assert.equal(authorityCodec.read(completed.payload)?.phase, 'sourceCleanupCompleted');

  events.length = 0;
  const retried = await writer.complete(
    { id: 'source-response-loss', key },
    committed
  );
  assert.equal(retried.storeVersion.value, completed.storeVersion.value);
  assert.deepEqual(events, ['payload-delete']);
});

test('source cleanup conflict deletes only completed orphan and retains pending root', async () => {
  const events: string[] = [];
  const authority = authorityStore();
  const key = authorityKey('spot:source-conflict');
  const initial = await createAuthority(authority, 'spot:source-conflict');
  const store = new MemoryRelocationStore(events);
  const durable = new ServiceDurableRelocationRuntime(authority, store, authorityCodec);
  const published = await durable.captureAndPublish(
    key,
    initial,
    owner('owner-b', 2n),
    relocationEnvelope()
  );
  const fence = await reserveTarget(authority, key, published.authority, 'source-conflict');
  const committed = await durable.commitOwner(
    key,
    published.authority,
    owner('owner-b', 2n),
    fence
  );
  const concurrent = await authority.compareExchangeAuthority(
    key,
    committed.storeVersion,
    {
      kind: 'put',
      generationTransition: 'preserve',
      payload: committed.payload
    }
  );
  assert.equal(concurrent.kind, 'stored');
  const writer = new ServiceRelocationSourceAuthorityWriter(
    durable,
    (staging: { readonly id: string; readonly key: ZLinkAuthorityKey }) => staging.key
  );

  events.length = 0;
  await assert.rejects(
    writer.complete({ id: 'source-conflict', key }, committed),
    /rejected source-cleanup completion/
  );
  assert.deepEqual(events, ['payload-put', 'payload-delete']);
  const current = await authority.readAuthority(key);
  assert.equal(current.kind, 'snapshot');
  if (current.kind !== 'snapshot') return;
  const pending = authorityCodec.read(current.payload);
  assert.equal(pending?.phase, 'sourceCleanupPending');
  assert.equal(pending?.reference, published.publication.reference);
  assert.equal((await store.get(published.publication.reference)).kind, 'found');
});

test('concrete User Spot aggregate restores hidden membership and sealed work before authority commit', async () => {
  const events: string[] = [];
  const captureOwner = new ServiceRelocationObjectCaptureOwner();
  const spotUnit = captureUnit(events, {
    authorityKey: 'spot:room',
    objectKind: 'user_spot',
    stableType: 'room',
    objectGeneration: 3n,
    authorityOwnerGeneration: 2n,
    state: 'spot-state',
    journal: 'spot-journal',
    queuedMessages: [
      { sequence: 1n, payload: Buffer.from('first') },
      { sequence: 2n, payload: Buffer.from('second') }
    ],
    timers: relocationEnvelope().participants[0]!.timers
  });
  const actorUnit = captureUnit(events, {
    authorityKey: 'actor:a',
    objectKind: 'actor',
    stableType: 'player',
    objectGeneration: 8n,
    authorityOwnerGeneration: 5n,
    state: 'actor-state',
    journal: 'actor-journal',
    queuedMessages: [],
    timers: []
  });
  const membership = relocationEnvelope().memberships;
  const captured = await captureOwner.captureUserSpotAggregate(
    '11111111-1111-4111-8111-111111111111',
    1n,
    spotUnit,
    [actorUnit],
    membership
  );
  assert.deepEqual(events, [
    'source-seal:spot:room',
    'source-seal:actor:a',
    'source-capture:spot:room',
    'source-capture:actor:a'
  ]);

  const authority = authorityStore();
  const key = authorityKey('spot:concrete-aggregate');
  const initial = await createAuthority(authority, 'spot:concrete-aggregate');
  const authorityPort = {
    readAuthority: (...args: Parameters<ZLinkInMemoryAuthorityStore['readAuthority']>) =>
      authority.readAuthority(...args),
    compareExchangeAuthority: (
      ...args: Parameters<ZLinkInMemoryAuthorityStore['compareExchangeAuthority']>
    ) => {
      events.push('authority-cas');
      return authority.compareExchangeAuthority(...args);
    }
  };
  const store = new MemoryRelocationStore(events);
  const durable = new ServiceDurableRelocationRuntime(authorityPort, store, authorityCodec);
  const restoreOwner = new ServiceRelocationObjectRestoreOwner({
    async createHidden(participant) {
      events.push(`target-factory:${participant.key}:${participant.objectGeneration}`);
      return { authorityKey: participant.key };
    },
    async restoreApplicationState(hidden, payload) {
      events.push(`target-restore:${hidden.authorityKey}:${Buffer.from(payload).toString()}`);
    },
    async restoreMemberships(hidden, memberships) {
      assert.equal(hidden.size, 2);
      assert.equal(memberships[0]?.membershipEpoch, 4n);
      events.push('target-membership:actor:a:spot:room:4');
    },
    async publish(hidden) {
      events.push(`target-publish:${hidden.authorityKey}`);
    },
    async replayAcceptedJournal(hidden) {
      events.push(`target-journal:${hidden.authorityKey}`);
    },
    async replayQueuedMessage(hidden, message) {
      events.push(`target-queue:${hidden.authorityKey}:${message.sequence}`);
    },
    async restoreTimer(hidden, timer) {
      events.push(`target-timer:${hidden.authorityKey}:${timer.timerId}`);
    },
    async normalize(hidden) {
      events.push(`target-normalize:${hidden.authorityKey}`);
    },
    async openAdmission(hidden) {
      events.push(`target-admission:${hidden.authorityKey}`);
    },
    abort(hidden) {
      events.push(`target-abort:${hidden.authorityKey}`);
    }
  }, () => key);
  const writer = new ServiceRelocationSourceAuthorityWriter<
    ServiceObjectRelocationStaging<{ readonly authorityKey: string }>
  >(
    durable,
    staging => staging.primaryAuthorityKey
  );
  const coordinator = new ServiceRelocationCoordinator(
    durable,
    restoreOwner,
    new ServiceCapturedRelocationSourceCompletion(captured, writer),
    {
      async relayCaptured() {
        events.push('terminal-reply-relay');
      }
    },
    {
      async replace() {
        events.push('session-route-replace');
      }
    },
    {
      async waitUntilReleasable() {
        events.push('recovery-releasable');
      }
    }
  );

  events.length = 0;
  await coordinator.captureRestoreAndCommit(
    key,
    initial,
    owner('owner-b', 2n),
    captured.envelope,
    published => reserveTarget(authority, key, published, 'concrete-aggregate')
  );
  const rootPublication = events.indexOf('authority-cas', events.indexOf('payload-put'));
  const ownerCommit = events.lastIndexOf('authority-cas');
  assert.ok(rootPublication < events.indexOf('target-restore:actor:a:actor-state'));
  assert.ok(ownerCommit > events.indexOf('target-membership:actor:a:spot:room:4'));
  assert.ok(ownerCommit > events.indexOf('target-restore:actor:a:actor-state'));
  assert.ok(events.indexOf('target-queue:spot:room:2') < events.indexOf('source-commit:spot:room'));
  assert.ok(events.indexOf('target-timer:spot:room:idle') < events.indexOf('source-commit:spot:room'));
  assert.ok(events.indexOf('source-commit:actor:a') < events.indexOf('terminal-reply-relay'));
  assert.ok(events.indexOf('session-route-replace') < events.indexOf('target-admission:spot:room'));
  assert.ok(events.indexOf('target-normalize:actor:a') < events.indexOf('target-admission:actor:a'));
  assert.ok(events.indexOf('target-admission:actor:a') < events.indexOf('recovery-releasable'));
});

test('concrete standalone Actor owner preserves external membership and exact generation', async () => {
  const events: string[] = [];
  const captureOwner = new ServiceRelocationObjectCaptureOwner();
  const actor = captureUnit(events, {
    authorityKey: 'actor:solo',
    objectKind: 'actor',
    stableType: 'player',
    objectGeneration: 13n,
    authorityOwnerGeneration: 7n,
    state: 'solo-state',
    journal: 'solo-journal',
    queuedMessages: [{ sequence: 9n, payload: Buffer.from('solo-job') }],
    timers: []
  });
  const captured = await captureOwner.captureStandaloneActor(
    '33333333-3333-4333-8333-333333333333',
    4n,
    actor,
    {
      actorKey: 'actor:solo',
      spotKey: 'entry:node-a',
      spotObjectGeneration: 6n,
      membershipEpoch: 11n
    }
  );
  const restored: string[] = [];
  const ownerRuntime = new ServiceRelocationObjectRestoreOwner({
    async createHidden(participant) {
      restored.push(`factory:${participant.objectGeneration}:${participant.authorityOwnerGeneration}`);
      return { authorityKey: participant.key };
    },
    async restoreApplicationState(_hidden, payload) {
      restored.push(`state:${Buffer.from(payload).toString()}`);
    },
    async restoreMemberships(_hidden, memberships) {
      restored.push(`membership:${memberships[0]?.spotKey}:${memberships[0]?.membershipEpoch}`);
    },
    async publish() {},
    async replayAcceptedJournal() {},
    async replayQueuedMessage() {},
    async restoreTimer() {},
    async normalize() {},
    async openAdmission() {},
    abort() {
      restored.push('abort');
    }
  }, authorityKey);
  const staging = await ownerRuntime.prepare(captured.envelope);
  assert.deepEqual(restored, [
    'factory:13:7',
    'state:solo-state',
    'membership:entry:node-a:11'
  ]);
  assert.equal(staging.envelope.participants[0]?.queuedMessages[0]?.sequence, 9n);
  await ownerRuntime.abort(staging);
  assert.equal(restored.at(-1), 'abort');
  await captured.abortSource();
  assert.equal(events.at(-1), 'source-abort:actor:solo');
});

test('concrete Instance Spot aggregate preserves Actor membership and relocation payload', async () => {
  const events: string[] = [];
  const captureOwner = new ServiceRelocationObjectCaptureOwner();
  const spot = captureUnit(events, {
    authorityKey: 'instance:match:42',
    objectKind: 'instance_spot',
    stableType: 'match',
    objectGeneration: 5n,
    authorityOwnerGeneration: 8n,
    state: 'instance-state',
    journal: 'instance-journal',
    queuedMessages: [],
    timers: []
  });
  const actor = captureUnit(events, {
    authorityKey: 'actor:player',
    objectKind: 'actor',
    stableType: 'player',
    objectGeneration: 9n,
    authorityOwnerGeneration: 4n,
    state: 'actor-state',
    journal: 'actor-journal',
    queuedMessages: [],
    timers: []
  });

  const captured = await captureOwner.captureInstanceSpotAggregate(
    '44444444-4444-4444-8444-444444444444',
    3n,
    spot,
    [actor],
    [{
      actorKey: 'actor:player',
      spotKey: 'instance:match:42',
      spotObjectGeneration: 5n,
      membershipEpoch: 12n
    }]
  );

  assert.deepEqual(
    captured.envelope.participants.map(participant => participant.objectKind),
    ['instance_spot', 'actor']
  );
  assert.equal(captured.envelope.memberships[0]?.membershipEpoch, 12n);
  await captured.abortSource();
  assert.deepEqual(events.slice(-2), [
    'source-abort:actor:player',
    'source-abort:instance:match:42'
  ]);
});

test('concrete target Restore failure leaves authority and Relocation Store unpublished', async () => {
  const events: string[] = [];
  const authority = authorityStore();
  const key = authorityKey('spot:hidden-restore-failure');
  const initial = await createAuthority(authority, 'spot:hidden-restore-failure');
  const store = new MemoryRelocationStore(events);
  const durable = new ServiceDurableRelocationRuntime(authority, store, authorityCodec);
  const restoreOwner = new ServiceRelocationObjectRestoreOwner({
    async createHidden(participant) {
      events.push(`factory:${participant.key}`);
      return { authorityKey: participant.key };
    },
    async restoreApplicationState(hidden) {
      events.push(`restore:${hidden.authorityKey}`);
      throw new Error('application Restore rejected payload');
    },
    async restoreMemberships() {},
    async publish() {},
    async replayAcceptedJournal() {},
    async replayQueuedMessage() {},
    async restoreTimer() {},
    async normalize() {},
    async openAdmission() {},
    abort(hidden) {
      events.push(`abort:${hidden.authorityKey}`);
    }
  }, () => key);
  const coordinator = new ServiceRelocationCoordinator(
    durable,
    restoreOwner,
    {
      async complete(_staging, authoritySnapshot) {
        return authoritySnapshot;
      }
    },
    { async relayCaptured() {} },
    { async replace() {} },
    { async waitUntilReleasable() {} }
  );

  await assert.rejects(
    coordinator.captureRestoreAndCommit(
      key,
      initial,
      owner('owner-b', 2n),
      relocationEnvelope()
    ),
    /Restore rejected payload/
  );
  assert.deepEqual(events, [
    'payload-put',
    'factory:spot:room',
    'restore:spot:room',
    'abort:spot:room',
    'payload-delete'
  ]);
  const current = await authority.readAuthority(key);
  assert.equal(current.kind, 'snapshot');
  if (current.kind === 'snapshot') {
    assert.notEqual(current.storeVersion.value, initial.storeVersion.value);
    assert.equal(current.objectGeneration, initial.objectGeneration);
    assert.equal(current.authorityOwnerGeneration, initial.authorityOwnerGeneration);
    assert.equal(current.ownerId, initial.ownerId);
    assert.equal(current.ownerLeaseGeneration, initial.ownerLeaseGeneration);
    assert.equal(authorityCodec.read(current.payload), undefined);
  }
});

test('target restore replays and completes before route replacement normalization and admission', async () => {
  const events: string[] = [];
  let releaseRecovery!: () => void;
  const recoveryGate = new Promise<void>(resolve => {
    releaseRecovery = resolve;
  });
  const authority = authorityStore();
  const key = authorityKey('spot:target-restore');
  const initial = await createAuthority(authority, 'spot:target-restore');
  const authorityPort = {
    readAuthority: (...args: Parameters<ZLinkInMemoryAuthorityStore['readAuthority']>) =>
      authority.readAuthority(...args),
    compareExchangeAuthority: (
      ...args: Parameters<ZLinkInMemoryAuthorityStore['compareExchangeAuthority']>
    ) => {
      events.push('authority-cas');
      return authority.compareExchangeAuthority(...args);
    }
  };
  const store = new MemoryRelocationStore(events);
  const durable = new ServiceDurableRelocationRuntime(authorityPort, store, authorityCodec);
  const published = await durable.captureAndPublish(
    key,
    initial,
    owner('owner-b', 2n),
    relocationEnvelope()
  );
  events.length = 0;
  const coordinator = new ServiceRelocationCoordinator(durable, {
    async prepare(envelope) {
      events.push('target-prepare');
      assert.equal(envelope.participants.length, 2);
      return { id: 'staging-room' };
    },
    async publish() {
      events.push('target-publish');
    },
    async replayAcceptedJournal() {
      events.push('accepted-journal-replay');
    },
    async normalize() {
      events.push('steady-normalize');
    },
    async openAdmission() {
      events.push('target-admission-open');
    },
    abort() {
      events.push('target-abort');
    }
  }, {
    async complete(_staging, authoritySnapshot) {
      events.push('source-cleanup-completed');
      return authoritySnapshot;
    }
  }, {
    async relayCaptured() {
      events.push('terminal-reply-relay');
    }
  }, {
    async replace() {
      events.push('session-route-replace');
    }
  }, {
    async waitUntilReleasable() {
      events.push('recovery-wait');
      await recoveryGate;
      events.push('recovery-releasable');
    }
  });
  const fence = await reserveTarget(authority, key, published.authority, 'target-restore');

  const completion = coordinator.restoreAndCommit(
    key,
    published.authority,
    owner('owner-b', 2n),
    fence
  );
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(events.at(-1), 'recovery-wait');
  assert.equal((await store.get(published.publication.reference)).kind, 'found');
  releaseRecovery();
  const committed = await completion;
  assert.deepEqual(events, [
    'target-prepare',
    'authority-cas',
    'target-publish',
    'accepted-journal-replay',
    'source-cleanup-completed',
    'terminal-reply-relay',
    'session-route-replace',
    'steady-normalize',
    'target-admission-open',
    'recovery-wait',
    'recovery-releasable',
    'authority-cas',
    'payload-delete'
  ]);
  assert.equal(committed.authority.ownerId, 'owner-b');
  assert.equal(
    committed.authority.authorityOwnerGeneration,
    published.authority.authorityOwnerGeneration + 1n
  );
});

test('post-commit route replacement failure preserves committed owner and relocation root', async () => {
  const events: string[] = [];
  const authority = authorityStore();
  const key = authorityKey('actor:post-commit');
  const initial = await createAuthority(authority, 'actor:post-commit');
  const store = new MemoryRelocationStore(events);
  const durable = new ServiceDurableRelocationRuntime(authority, store, authorityCodec);
  const published = await durable.captureAndPublish(
    key,
    initial,
    owner('owner-b', 2n),
    relocationEnvelope()
  );
  const fence = await reserveTarget(authority, key, published.authority, 'post-commit');
  let aborted = 0;
  let routeAttempts = 0;
  let admissionOpened = 0;
  let publishAttempts = 0;
  let replayAttempts = 0;
  let cleanupAttempts = 0;
  const coordinator = new ServiceRelocationCoordinator(durable, {
    async prepare() {
      return { id: 'staging-actor' };
    },
    async publish() {
      publishAttempts++;
    },
    async replayAcceptedJournal() {
      replayAttempts++;
    },
    async normalize() {},
    async openAdmission() {
      admissionOpened++;
    },
    abort() {
      aborted++;
    }
  }, {
    async complete(_staging, authoritySnapshot) {
      cleanupAttempts++;
      return authoritySnapshot;
    }
  }, {
    async relayCaptured() {}
  }, {
    async replace() {
      routeAttempts++;
      if (routeAttempts === 1) {
        throw new Error('session route replacement failed');
      }
    }
  }, {
    async waitUntilReleasable() {}
  });

  let postCommit: ServiceRelocationPostCommitError | undefined;
  await assert.rejects(
    () => coordinator.restoreAndCommit(
      key,
      published.authority,
      owner('owner-b', 2n),
      fence
    ),
    (error: unknown) => {
      if (!(error instanceof ServiceRelocationPostCommitError)) return false;
      postCommit = error;
      return true;
    }
  );
  assert.equal(aborted, 0);
  assert.equal(admissionOpened, 0);
  const current = await authority.readAuthority(key);
  assert.equal(current.kind, 'snapshot');
  if (current.kind !== 'snapshot') return;
  assert.equal(current.ownerId, 'owner-b');
  const publication = authorityCodec.read(current.payload);
  assert.ok(publication);
  assert.equal((await store.get(publication.reference)).kind, 'found');

  assert.ok(postCommit);
  const recovered = await coordinator.resumeCommitted(key, {
    staging: postCommit.staging as { id: string },
    authority: postCommit.authority
  });
  assert.equal(recovered.authority.ownerId, 'owner-b');
  assert.equal(routeAttempts, 2);
  assert.equal(publishAttempts, 2);
  assert.equal(replayAttempts, 2);
  assert.equal(cleanupAttempts, 2);
  assert.equal(admissionOpened, 1);
  assert.equal((await store.get(publication.reference)).kind, 'missing');
});

function captureUnit(
  events: string[],
  options: {
    readonly authorityKey: string;
    readonly objectKind: 'actor' | 'user_spot' | 'instance_spot';
    readonly stableType: string;
    readonly objectGeneration: bigint;
    readonly authorityOwnerGeneration: bigint;
    readonly state: string;
    readonly journal: string;
    readonly queuedMessages: readonly ServiceRelocationQueuedMessage[];
    readonly timers: readonly ServiceRelocationTimer[];
  }
): ServiceRelocationCaptureUnit {
  return {
    authorityKey: options.authorityKey,
    objectKind: options.objectKind,
    stableType: options.stableType,
    objectGeneration: options.objectGeneration,
    authorityOwnerGeneration: options.authorityOwnerGeneration,
    async seal() {
      events.push(`source-seal:${options.authorityKey}`);
      return {
        acceptedJournal: Buffer.from(options.journal),
        queuedMessages: options.queuedMessages,
        timers: options.timers
      };
    },
    async captureApplicationState() {
      events.push(`source-capture:${options.authorityKey}`);
      return Buffer.from(options.state);
    },
    commitSeal() {
      events.push(`source-commit:${options.authorityKey}`);
    },
    abortSeal() {
      events.push(`source-abort:${options.authorityKey}`);
    }
  };
}

function authorityStore(): ZLinkInMemoryAuthorityStore {
  return new ZLinkInMemoryAuthorityStore({
    isTargetLive: () => true
  }, () => new Date(100));
}

async function reserveTarget(
  authority: ZLinkInMemoryAuthorityStore,
  key: ZLinkAuthorityKey,
  current: ZLinkAuthoritySnapshot,
  reservationName: string
) {
  const result = await authority.reserveRelocationCapacity({
    reservationId: reservationName === 'target-restore'
      ? '11111111-1111-4111-8111-111111111111'
      : '22222222-2222-4222-8222-222222222222',
    authorityKey: key,
    expectedStoreVersion: current.storeVersion,
    objectKind: 'user_spot',
    stableType: 'room',
    sourceDescriptor: current.allocation.descriptor,
    sourceNodeLifecycleGeneration: current.allocation.descriptorLifecycleGeneration,
    sourceOwner: owner(current.ownerId, current.ownerLeaseGeneration),
    targetDescriptor: { meshName: 'mesh', rid: 'node-b' },
    targetNodeLifecycleGeneration: 2n,
    targetOwner: owner('owner-b', 2n),
    capacity: {
      actors: 0,
      spots: 1,
      spotType: { objectKind: 'user_spot', stableType: 'room', count: 1 }
    }
  });
  assert.equal(result.kind, 'reserved');
  if (result.kind !== 'reserved') {
    throw new Error('Relocation reservation failed.');
  }
  return result.fence;
}

async function createAuthority(
  authority: ZLinkInMemoryAuthorityStore,
  globalId: string
): Promise<ZLinkAuthoritySnapshot> {
  const target: ZLinkObjectCreationTarget = {
    meshName: 'mesh',
    nodeRid: 'node-a',
    nodeLifecycleGeneration: 1n,
    owner: owner('owner-a', 1n)
  };
  const reserved = await authority.reserve({
    key: { kind: 'user_spot', globalId },
    intent: {
      stableType: 'room',
      requestContentReference: `request:${globalId}`,
      requestSha256: Buffer.alloc(32, 1),
      requestEncodedSize: 10n
    },
    target,
    creatingPayload: Buffer.from('creating'),
    capacity: {
      actors: 0,
      spots: 1,
      spotType: {
        objectKind: 'user_spot',
        stableType: 'room',
        count: 1
      }
    }
  });
  assert.equal(reserved.kind, 'reserved');
  if (reserved.kind !== 'reserved') throw new Error('Authority reservation failed.');
  const committed = await authority.commit({
    key: { kind: 'user_spot', globalId },
    reservationId: reserved.reservationId,
    expectedStoreVersion: reserved.creating.storeVersion.value,
    target,
    readyPayload: Buffer.from('owner-state')
  });
  assert.equal(committed.kind, 'committed');
  if (committed.kind !== 'committed') throw new Error('Authority commit failed.');
  return committed.ready;
}

function authorityKey(value: string): ZLinkAuthorityKey {
  return encodeAuthorityKey('user_spot', value);
}

function owner(ownerId: string, leaseGeneration: bigint): ZLinkLocationOwnerToken {
  return { ownerId, leaseGeneration };
}

function relocationEnvelope(): ServiceRelocationEnvelope {
  return {
    aggregateId: '11111111-1111-4111-8111-111111111111',
    aggregateGeneration: 1n,
    sourceCleanup: 'pending',
    memberships: [{
      actorKey: 'actor:a',
      spotKey: 'spot:room',
      spotObjectGeneration: 3n,
      membershipEpoch: 4n
    }],
    participants: [
      {
        key: 'spot:room',
        objectKind: 'user_spot',
        stableType: 'room',
        objectGeneration: 3n,
        authorityOwnerGeneration: 2n,
        applicationState: Buffer.from('spot-state'),
        acceptedJournal: Buffer.from('spot-journal'),
        replayCursor: 0n,
        terminalReplies: Buffer.alloc(0),
        pendingRelayCount: 0,
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
      },
      {
        key: 'actor:a',
        objectKind: 'actor',
        stableType: 'player',
        objectGeneration: 8n,
        authorityOwnerGeneration: 5n,
        applicationState: Buffer.from('actor-state'),
        acceptedJournal: Buffer.from('actor-journal'),
        replayCursor: 0n,
        terminalReplies: Buffer.alloc(0),
        pendingRelayCount: 0,
        queuedMessages: [],
        timers: []
      }
    ]
  };
}

const authorityCodec = new ServiceRelocationAuthorityPayloadCodec();

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
