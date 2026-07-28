import assert from 'node:assert/strict';
import { test } from 'node:test';
import { Message, SubmitResult } from '@zlink-systems/zlink';
import type { ZLinkAuthoritySnapshot } from '../../packages/framework/src/contracts';
import { ZLinkHostServiceRelocationRuntime } from '../../packages/framework/src/runtime/host/service-relocation-host-runtime';
import {
  type ZLinkServiceRelocationControlRequest,
  type ZLinkServiceRelocationControlResponse
} from '../../packages/framework/src/runtime/host/service-relocation-control';
import {
  crc32c,
  encodeServiceRelocationEnvelope,
  ServiceRelocationAuthorityPayloadCodec,
  type ServiceRelocationEnvelope
} from '../../packages/framework/src/runtime/foundation/service-relocation-runtime';
import { encodeAuthorityKey } from '../../packages/framework/src/runtime/locations/authority-key-codec';
import {
  decodeMaintenanceReplyRelayAck,
  encodeMaintenanceReplyRelay,
  encodeMaintenanceReplyRelayAck,
  encodeServiceWireFrozenActorApplicationRecord,
  type ServiceMaintenanceReplyRelay,
  type ServiceWireRequestSourceFence,
  type ServiceMaintenanceRelocationPrepare,
  type ServiceWireRelocationCandidate,
  type ServiceWireRelocationCoordinatorFence,
  type ServiceWireRelocationObject,
  type ServiceWireRelocationParticipant
} from '../../packages/framework/src/runtime/foundation/service-stateful-wire-codec';

const spotKey = encodeAuthorityKey('user_spot', 'spot-a').value;
const actorKey = encodeAuthorityKey('actor', 'actor-a').value;

test('two host owners exchange canonical relocation reservation publish replay and seal commands', async () => {
  const events: string[] = [];
  const commands: number[] = [];
  const envelope = relocationEnvelope();
  const encodedEnvelope = encodeServiceRelocationEnvelope(envelope);
  const root = { reference: 'shared-root-a', checksumCrc32c: crc32c(encodedEnvelope) };
  const zeroEnvelope: ServiceRelocationEnvelope = {
    ...envelope,
    aggregateGeneration: 2n,
    participants: envelope.participants.map(participant => ({
      ...participant,
      queuedMessages: []
    }))
  };
  const encodedZeroEnvelope = encodeServiceRelocationEnvelope(zeroEnvelope);
  const zeroRoot = { reference: 'shared-root-zero', checksumCrc32c: crc32c(encodedZeroEnvelope) };
  const abortEnvelope: ServiceRelocationEnvelope = { ...envelope, aggregateGeneration: 3n };
  const encodedAbortEnvelope = encodeServiceRelocationEnvelope(abortEnvelope);
  const abortRoot = { reference: 'shared-root-abort', checksumCrc32c: crc32c(encodedAbortEnvelope) };
  const publication = {
    phase: 'sourceCleanupPending' as const,
    reference: root.reference,
    checksumCrc32c: root.checksumCrc32c,
    aggregateId: envelope.aggregateId,
    aggregateGeneration: envelope.aggregateGeneration,
    inventoryDigest: '0'.repeat(64),
    targetOwnerId: 'owner-target',
    targetOwnerLeaseGeneration: 8n
  };
  const authorityPayload = new ServiceRelocationAuthorityPayloadCodec().publish(
    Buffer.from('authority-state'),
    publication
  );
  const authorities = new Map<string, ZLinkAuthoritySnapshot>([
    [spotKey, relocationAuthority('user_spot', 'SpotType', authorityPayload)],
    [actorKey, relocationAuthority('actor', 'ActorType')]
  ]);
  let backpressureFirstControl = true;
  let source!: ZLinkHostServiceRelocationRuntime;
  const deliver = async (
    runtime: ZLinkHostServiceRelocationRuntime,
    sourceNodeRid: string,
    payload: Uint8Array
  ): Promise<void> => {
    const part = Message.from(payload);
    try {
      assert.equal(await runtime.tryHandleControl('mesh-a', {
        sourceNodeRid,
        parts: [part]
      } as never), true);
    } finally {
      part.close();
    }
  };
  const target = new ZLinkHostServiceRelocationRuntime({
    registration: { spotNodes: new Map([['mesh-a', {
      spotFactoryRegistrations: {
        SpotType: { implementation: class {}, relocation: { kind: 'recreate' } }
      },
      actorFactoryRegistrations: {
        ActorType: { implementation: class {}, relocation: { kind: 'recreate' } }
      }
    }]]) },
    locationStore: () => ({
      readAuthority: async (key: { readonly value: string }) => authorities.get(key.value)!,
      prepareAggregate: async (request: { readonly aggregateId: { readonly value: string };
        readonly aggregateGeneration: bigint }) => {
        events.push('reserve-capacity');
        return { kind: 'prepared' as const, fence: {
          aggregateId: request.aggregateId,
          aggregateGeneration: request.aggregateGeneration
        } };
      },
      commitAggregate: async () => {
        events.push('commit-capacity');
        for (const [key, current] of authorities) {
          authorities.set(key, {
            ...current,
            storeVersion: { value: `committed-${key}` } as never,
            authorityOwnerGeneration: 2n,
            ownerId: 'owner-target',
            ownerLeaseGeneration: 8n,
            payload: key === spotKey
              ? authorityPayload
              : new ServiceRelocationAuthorityPayloadCodec().publish(
                  current.payload,
                  publication
                )
          });
        }
        return { kind: 'committed' as const };
      },
      abortAggregate: async () => {
        events.push('abort-capacity');
        return { kind: 'aborted' as const };
      }
    }),
    relocationStore: () => ({
      read: async (reference: { readonly value: string }) =>
        reference.value === root.reference
          ? foundBlob(encodedEnvelope)
          : reference.value === zeroRoot.reference
            ? foundBlob(encodedZeroEnvelope)
          : reference.value === abortRoot.reference
            ? foundBlob(encodedAbortEnvelope)
          : { kind: 'missing' as const, storeNow: new Date() }
    }),
    currentOwner: () => ({ ownerId: 'owner-target', leaseGeneration: 8n }),
    liveDescriptors: async () => [],
    meshNode: () => ({
      status: () => ({ routingId: 'node-target', lifecycleGeneration: 12n }),
      sendToNode: (nodeRid: string, payload: Uint8Array) => {
        assert.equal(nodeRid, 'node-source');
        void deliver(source, 'node-target', payload);
        return SubmitResult.Ok;
      }
    }),
    completions: () => undefined,
    spotManager: () => ({
      prepareRelocationSpot: async () => {
        events.push('prepare-spot');
        return { spotId: 'spot-a', spot: {}, timers: { restoreRelocation: () => undefined },
          commitActorJoin: () => events.push('membership') };
      },
      publishRelocationSpot: async () => events.push('publish-spot'),
      abortRelocationSpot: async () => events.push('abort-spot'),
      dispatchRoutedActorPacket: async () => undefined
    }),
    actorManager: () => ({
      prepareRelocationActor: async () => {
        events.push('prepare-actor');
        return { context: { actorId: 'actor-a' }, configure() {} };
      },
      getState: () => ({
        spotId: 'spot-a',
        setJoinedSpot: () => events.push('joined'),
        setLocationGeneration: () => events.push('actor-authority'),
        setOwnerLeaseGeneration: () => undefined
      }),
      publishRelocationActor: () => events.push('publish-actor'),
      abortRelocationActor: () => events.push('abort-actor')
    }),
    actorTransfer: {} as never
  } as never);
  source = new ZLinkHostServiceRelocationRuntime({
    registration: { spotNodes: new Map() },
    locationStore: () => undefined,
    relocationStore: () => undefined,
    currentOwner: () => ({ ownerId: 'owner-source', leaseGeneration: 7n }),
    liveDescriptors: async () => [],
    meshNode: () => ({
      status: () => ({ routingId: 'node-source', lifecycleGeneration: 11n }),
      sendToNode: (nodeRid: string, payload: Uint8Array) => {
        assert.equal(nodeRid, 'node-target');
        commands.push(payload[3]!);
        if (backpressureFirstControl) {
          backpressureFirstControl = false;
          return SubmitResult.Backpressured;
        }
        void deliver(target, 'node-source', payload);
        return SubmitResult.Ok;
      }
    }),
    completions: () => undefined,
    spotManager: () => undefined,
    actorManager: () => undefined,
    actorTransfer: {} as never
  } as never);

  const roundTrip = async (
    request: ZLinkServiceRelocationControlRequest
  ): Promise<ZLinkServiceRelocationControlResponse> => {
    return await (source as unknown as {
      sendControl(
        meshName: string,
        targetNodeRid: string,
        value: ZLinkServiceRelocationControlRequest
      ): Promise<ZLinkServiceRelocationControlResponse>;
    }).sendControl('mesh-a', 'node-target', request);
  };

  const prepare = relocationPrepare(envelope, root);
  const offered = await roundTrip(prepare);
  assert.equal(offered.kind, 'ready');
  if (offered.kind !== 'ready') return;
  assert.deepEqual(events, []);
  assert.deepEqual(await roundTrip(prepare), offered);
  assert.deepEqual(events, []);
  const zeroPrepare = relocationPrepare(zeroEnvelope, zeroRoot);
  const parallelOffer = await roundTrip(zeroPrepare);
  assert.equal(parallelOffer.kind, 'ready');
  if (parallelOffer.kind !== 'ready') return;
  assert.equal(parallelOffer.offeredMessages, 64n);
  assert.equal(parallelOffer.offeredBytes, 256n * 1024n * 1024n);
  const reserved = await roundTrip({ ...offered, role: 'source',
    offeredMessages: 0n, offeredBytes: 0n, participants: prepare.participants });
  assert.equal(reserved.kind, 'reserved');
  assert.deepEqual(events, ['reserve-capacity']);
  const prepared = await roundTrip({ kind: 'data', relocation: prepare.relocation,
    targetAttemptGeneration: prepare.targetAttemptGeneration, coordinator: prepare.coordinator,
    senderRole: 'source', participantId: 1n, sequence: 1n, source: sourceFence(),
    object: prepare.object, phase: 'prepared' });
  assert.equal(prepared.kind, 'ack');
  assert.deepEqual(events, [
    'reserve-capacity', 'prepare-spot', 'prepare-actor', 'joined', 'membership'
  ]);
  const committed = await roundTrip({ kind: 'data', relocation: prepare.relocation,
    targetAttemptGeneration: prepare.targetAttemptGeneration, coordinator: prepare.coordinator,
    senderRole: 'source', participantId: 1n, sequence: 1n, source: sourceFence(),
    object: prepare.object, phase: 'committed' });
  assert.equal(committed.kind, 'ack');
  assert.equal(events.at(-1), 'commit-capacity');
  const published = await roundTrip({ kind: 'complete', relocation: prepare.relocation,
    targetAttemptGeneration: prepare.targetAttemptGeneration, coordinator: prepare.coordinator,
    senderRole: 'source', source: sourceFence(), sourceCleanupState: 'pending' });
  assert.equal(published.kind, 'complete');

  const ack = await roundTrip({ kind: 'data', relocation: prepare.relocation,
    targetAttemptGeneration: prepare.targetAttemptGeneration, coordinator: prepare.coordinator,
    senderRole: 'source', participantId: 1n, sequence: 1n,
    frozenRecord: acceptedFrozenRecord(envelope, prepare.coordinator, prepare.candidate) });
  assert.equal(ack.kind, 'ack');
  if (ack.kind !== 'ack') return;
  const highWater = [
    { participantId: ack.participantId, highWater: ack.highWater },
    { participantId: 2n, highWater: 0n }
  ];
  const sealed = await roundTrip({ kind: 'seal', relocation: prepare.relocation,
    targetAttemptGeneration: prepare.targetAttemptGeneration, coordinator: prepare.coordinator,
    senderRole: 'source', response: true, participants: highWater });
  assert.equal(sealed.kind, 'seal');
  assert.deepEqual(commands, [40, 40, 40, 40, 30, 31, 31, 35, 31, 34]);
  assert.deepEqual(events, [
    'reserve-capacity', 'prepare-spot', 'prepare-actor', 'joined', 'membership',
    'commit-capacity',
    'publish-spot', 'actor-authority', 'publish-actor'
  ]);

  const zeroOffer = await roundTrip(zeroPrepare);
  assert.equal(zeroOffer.kind, 'ready');
  if (zeroOffer.kind !== 'ready') return;
  assert.equal(zeroOffer.offeredMessages, 64n);
  assert.equal(zeroOffer.offeredBytes, 256n * 1024n * 1024n);
  assert.deepEqual(zeroOffer.participants, []);

  const targetInternals = target as unknown as {
    targetOffers: Map<string, unknown>;
    expireTargetOffer(stagingId: string, offer: unknown): Promise<void>;
  };
  const [zeroStagingId, zeroStoredOffer] = [...targetInternals.targetOffers.entries()][0]!;
  await targetInternals.expireTargetOffer(zeroStagingId, zeroStoredOffer);
  assert.equal(targetInternals.targetOffers.size, 0);

  const abortPublication = {
    ...publication,
    reference: abortRoot.reference,
    checksumCrc32c: abortRoot.checksumCrc32c,
    aggregateGeneration: abortEnvelope.aggregateGeneration
  };
  authorities.set(spotKey, relocationAuthority(
    'user_spot',
    'SpotType',
    new ServiceRelocationAuthorityPayloadCodec().publish(
      Buffer.from('authority-state'),
      abortPublication
    )
  ));
  authorities.set(actorKey, relocationAuthority('actor', 'ActorType'));
  const abortPrepare = relocationPrepare(abortEnvelope, abortRoot);
  const abortOffer = await roundTrip(abortPrepare);
  assert.equal(abortOffer.kind, 'ready');
  if (abortOffer.kind !== 'ready') return;
  await roundTrip({ ...abortOffer, role: 'source', offeredMessages: 0n,
    offeredBytes: 0n, participants: abortPrepare.participants });
  await roundTrip({ kind: 'data', relocation: abortPrepare.relocation,
    targetAttemptGeneration: abortPrepare.targetAttemptGeneration,
    coordinator: abortPrepare.coordinator, senderRole: 'source', participantId: 1n,
    sequence: 1n, source: sourceFence(), object: abortPrepare.object, phase: 'prepared' });
  const abortStart = events.length;
  const abortControl = { kind: 'data' as const, relocation: abortPrepare.relocation,
    targetAttemptGeneration: abortPrepare.targetAttemptGeneration,
    coordinator: abortPrepare.coordinator, senderRole: 'source' as const, participantId: 1n,
    sequence: 1n, source: sourceFence(), object: abortPrepare.object, phase: 'aborted' as const };
  const aborted = await roundTrip(abortControl);
  assert.equal(aborted.kind, 'ack');
  assert.deepEqual(events.slice(abortStart), ['abort-actor', 'abort-spot', 'abort-capacity']);
  await roundTrip(abortControl);
  assert.deepEqual(events.slice(abortStart), ['abort-actor', 'abort-spot', 'abort-capacity']);
});

test('two host owners exchange independent reply relay and ACK with exact durable source fencing', async () => {
  const sourceFence: ServiceWireRequestSourceFence = {
    ownerId: 'owner-source', leaseGeneration: 7n,
    nodeRid: 'node-source', nodeGeneration: 11n
  };
  const coordinator: ServiceWireRelocationCoordinatorFence = {
    ownerId: 'owner-target', leaseGeneration: 8n,
    nodeRid: 'node-target', nodeGeneration: 12n,
    expectedAuthorityStoreVersion: 'version-target'
  };
  const relay: ServiceMaintenanceReplyRelay = {
    relocation: { high: 0x1111111111114111n, low: 0x8111111111111111n },
    targetAttemptGeneration: 1n,
    coordinator,
    operation: { high: 1n, low: 2n },
    replyRouteId: 3n,
    participantId: 1n,
    sequence: 1n,
    terminalResult: 0,
    failureCode: 0,
    payload: {
      packetName: 'zlink.relocation.reply',
      contentType: 'application/json',
      bytes: Buffer.from('{"accepted":true}')
    }
  };
  const relayAuthority: ZLinkAuthoritySnapshot = {
    ...relocationAuthority(),
    storeVersion: { value: 'version-target' } as never,
    payload: new ServiceRelocationAuthorityPayloadCodec().publish(
      Buffer.from('authority-state'),
      {
        phase: 'sourceCleanupCompleted',
        reference: 'completed-root',
        checksumCrc32c: 1,
        aggregateId: '11111111-1111-4111-8111-111111111111',
        aggregateGeneration: 2n,
        inventoryDigest: '0'.repeat(64),
        targetOwnerId: 'owner-target',
        targetOwnerLeaseGeneration: 8n
      }
    )
  };
  const commands: number[] = [];
  let accepted = 0;
  let dropFirstAck = true;
  let corruptSecondAckRoute = true;
  let sourceLeaseExpired = false;
  let staleAdmittedSource = false;
  let staleCoordinatorAuthority = false;
  let source!: ZLinkHostServiceRelocationRuntime;
  let target!: ZLinkHostServiceRelocationRuntime;
  const deliver = async (
    runtime: ZLinkHostServiceRelocationRuntime,
    sourceNodeRid: string,
    payload: Uint8Array
  ): Promise<void> => {
    const part = Message.from(payload);
    try {
      assert.equal(await runtime.tryHandleControl('mesh-a', {
        sourceNodeRid,
        parts: [part]
      } as never), true);
    } finally {
      part.close();
    }
  };
  source = new ZLinkHostServiceRelocationRuntime({
    registration: { spotNodes: new Map() },
    locationStore: () => ({ readAuthority: async () => staleCoordinatorAuthority
      ? { ...relayAuthority, storeVersion: { value: 'stale-version' } as never }
      : relayAuthority }),
    relocationStore: () => undefined,
    currentOwner: () => ({ ownerId: 'owner-source', leaseGeneration: 7n }),
    liveDescriptors: async () => [{ rid: 'node-target', lifecycleGeneration: 12n,
      ownerId: 'owner-target', leaseGeneration: 8n, state: 1 } as never],
    meshNode: () => ({
      status: () => ({ routingId: 'node-source', lifecycleGeneration: 11n }),
      sendToNode: (_nodeRid: string, payload: Uint8Array) => {
        commands.push(payload[3]!);
        if (payload[3] === 46 && dropFirstAck) {
          dropFirstAck = false;
          return SubmitResult.Ok;
        }
        if (payload[3] === 46 && corruptSecondAckRoute) {
          corruptSecondAckRoute = false;
          const ack = decodeMaintenanceReplyRelayAck(payload);
          void deliver(target, 'node-source',
            encodeMaintenanceReplyRelayAck({
              ...ack,
              replyRouteId: ack.replyRouteId + 1n
            }));
          return SubmitResult.Ok;
        }
        void deliver(target, 'node-source', payload);
        return SubmitResult.Ok;
      }
    }),
    completions: () => undefined,
    spotManager: () => undefined,
    actorManager: () => undefined,
    actorTransfer: {
      relayCanonicalMaintenanceTerminal: (
        operationId: string,
        replyRouteId: string,
        result: { readonly ok: boolean; readonly response?: unknown },
        targetNodeRid: string
      ) => {
        if (!['1:2', '1:5', '1:6'].includes(operationId) || replyRouteId !== '3'
          || targetNodeRid !== 'node-target') return { status: 'notAcknowledged' };
        assert.deepEqual(result, { index: 0, ok: true, response: { accepted: true } });
        accepted++;
        return { status: accepted === 1 ? 'terminalReceived' : 'alreadyTerminal', source: {
          ownerId: sourceFence.ownerId,
          ownerLeaseGeneration: sourceFence.leaseGeneration.toString(),
          nodeRid: sourceFence.nodeRid,
          nodeGeneration: sourceFence.nodeGeneration.toString(),
          replyRouteId: '3'
        } };
      }
    }
  } as never);
  (source as unknown as { relocationAuthorityKeys: Map<string, string> })
    .relocationAuthorityKeys.set(
      `${relay.relocation.high}:${relay.relocation.low}`,
      spotKey
    );
  target = new ZLinkHostServiceRelocationRuntime({
    registration: { spotNodes: new Map() },
    locationStore: () => ({
      readOwnerLease: async () => sourceLeaseExpired
        ? { kind: 'missing' as const }
        : { kind: 'found' as const,
            token: { ownerId: 'owner-source', leaseGeneration: 7n },
            leaseExpiresAt: new Date(2_000), storeNow: new Date(1_000) }
    }),
    relocationStore: () => undefined,
    currentOwner: () => ({ ownerId: 'owner-target', leaseGeneration: 8n }),
    liveDescriptors: async () => [{ rid: 'node-source',
      lifecycleGeneration: staleAdmittedSource ? 12n : 11n,
      ownerId: 'owner-source', leaseGeneration: 7n, state: 1 } as never],
    meshNode: () => ({
      status: () => ({ routingId: 'node-target', lifecycleGeneration: 12n }),
      sendToNode: (_nodeRid: string, payload: Uint8Array) => {
        commands.push(payload[3]!);
        void deliver(source, 'node-target', payload);
        return SubmitResult.Ok;
      }
    }),
    completions: () => undefined,
    spotManager: () => undefined,
    actorManager: () => undefined,
    actorTransfer: {} as never
  } as never);
  const sendReplyRelay = (target as unknown as {
    sendReplyRelay(
      meshName: string,
      targetNodeRid: string,
      request: ServiceMaintenanceReplyRelay,
      expectedSource: ServiceWireRequestSourceFence
    ): Promise<'terminalReceived' | 'alreadyTerminal' | 'sourceLeaseExpired'>;
  }).sendReplyRelay.bind(target);

  assert.equal(await sendReplyRelay('mesh-a', 'node-source', relay, sourceFence), 'alreadyTerminal');
  assert.deepEqual(commands, [33, 46, 33, 46, 33, 46]);
  assert.equal(accepted, 3);

  staleAdmittedSource = true;
  await assert.rejects(sendReplyRelay('mesh-a', 'node-source', {
    ...relay,
    operation: { high: 1n, low: 5n }
  }, sourceFence), /exact admitted source fence/);
  staleAdmittedSource = false;

  staleCoordinatorAuthority = true;
  await assert.rejects(
    deliver(source, 'node-target', encodeMaintenanceReplyRelay({
      ...relay,
      operation: { high: 1n, low: 6n }
    })),
    /coordinator authority fence is stale/
  );
  staleCoordinatorAuthority = false;

  await assert.rejects(
    deliver(source, 'node-target', encodeMaintenanceReplyRelay({
      ...relay,
      operation: { high: 1n, low: 3n }
    })),
    /collided/
  );

  sourceLeaseExpired = true;
  const beforeExpiry = commands.length;
  assert.equal(await sendReplyRelay('mesh-a', 'node-source', {
    ...relay,
    operation: { high: 1n, low: 4n }
  }, sourceFence), 'sourceLeaseExpired');
  assert.equal(commands.length, beforeExpiry);
});

function relocationPrepare(
  envelope: ServiceRelocationEnvelope,
  root: { readonly reference: string; readonly checksumCrc32c: number }
): ServiceMaintenanceRelocationPrepare {
  const coordinator: ServiceWireRelocationCoordinatorFence = {
    ownerId: 'owner-coordinator', leaseGeneration: 9n, nodeRid: 'node-coordinator',
    nodeGeneration: 13n, expectedAuthorityStoreVersion: 'version-a'
  };
  const candidate: ServiceWireRelocationCandidate = {
    nodeRid: 'node-target', nodeGeneration: 12n,
    ownerId: 'owner-target', ownerLeaseGeneration: 8n
  };
  const object: ServiceWireRelocationObject = {
    kind: 'userSpot', spotId: 'spot-a', objectGeneration: 1n,
    expectedAuthorityOwnerGeneration: 1n
  };
  const hasAcceptedRecord = envelope.participants[1]!.queuedMessages.length !== 0;
  const acceptedBytes = hasAcceptedRecord
    ? acceptedFrozenRecord(envelope, coordinator, candidate).canonicalBytes.byteLength
    : 0;
  const participants: readonly ServiceWireRelocationParticipant[] = [
    { participantId: 1n, allowanceMessages: hasAcceptedRecord ? 1n : 0n,
      allowanceBytes: BigInt(acceptedBytes) },
    { participantId: 2n, allowanceMessages: 0n, allowanceBytes: 0n }
  ];
  return { kind: 'prepare', relocation: { high: 0x1111111111114111n, low: 0x8111111111111111n },
    targetAttemptGeneration: envelope.aggregateGeneration, round: 'initial', coordinator,
    candidate, initiatorRole: 'source', object, sourceNodeRid: 'node-source',
    sourceNodeGeneration: 11n, requiredMessages: hasAcceptedRecord ? 1n : 0n,
    requiredBytes: BigInt(acceptedBytes),
    participants, root, applicationVersion: 1n };
}

function sourceFence() {
  return { ownerId: 'owner-source', leaseGeneration: 7n,
    nodeRid: 'node-source', nodeGeneration: 11n };
}

function acceptedFrozenRecord(
  envelope: ServiceRelocationEnvelope,
  coordinator: ServiceWireRelocationCoordinatorFence,
  candidate: ServiceWireRelocationCandidate
) {
  const participant = envelope.participants[1]!;
  const message = participant.queuedMessages[0]!;
  return encodeServiceWireFrozenActorApplicationRecord({
    source: {
      ownerId: coordinator.ownerId,
      leaseGeneration: coordinator.leaseGeneration,
      nodeRid: coordinator.nodeRid,
      nodeGeneration: coordinator.nodeGeneration
    },
    target: {
      actorId: 'actor-a',
      objectGeneration: participant.objectGeneration,
      nodeRid: candidate.nodeRid,
      nodeGeneration: candidate.nodeGeneration,
      authorityOwnerGeneration: participant.authorityOwnerGeneration + 1n,
      ownerLeaseGeneration: candidate.ownerLeaseGeneration
    },
    operationId: { high: 0n, low: 1n },
    payload: {
      packetName: '__zlink.actor.handoff.accepted',
      contentType: 'application/json',
      bytes: message.payload
    }
  });
}

function relocationEnvelope(): ServiceRelocationEnvelope {
  return { aggregateId: '11111111-1111-4111-8111-111111111111', aggregateGeneration: 1n,
    sourceCleanup: 'pending', participants: [
      { key: spotKey, objectKind: 'user_spot', stableType: 'SpotType',
        objectGeneration: 1n, authorityOwnerGeneration: 1n,
        applicationState: Buffer.alloc(0), acceptedJournal: Buffer.alloc(0), replayCursor: 0n,
        terminalReplies: Buffer.alloc(0), pendingRelayCount: 0, queuedMessages: [], timers: [] },
      { key: actorKey, objectKind: 'actor', stableType: 'ActorType',
        objectGeneration: 1n, authorityOwnerGeneration: 1n,
        applicationState: Buffer.alloc(0), acceptedJournal: Buffer.alloc(0), replayCursor: 0n,
        terminalReplies: Buffer.alloc(0), pendingRelayCount: 0,
        queuedMessages: [{ sequence: 1n, payload: Buffer.from(JSON.stringify({
          index: 0,
          header: Buffer.from('header').toString('base64'),
          payload: Buffer.from('payload').toString('base64'),
          returnResponse: false,
          operationId: '0:1',
          messageFollowHopCount: 0
        })) }], timers: [] }
    ], memberships: [{ actorKey, spotKey, spotObjectGeneration: 1n, membershipEpoch: 1n }] };
}

function relocationAuthority(
  objectKind: 'actor' | 'user_spot' = 'user_spot',
  stableType = 'SpotType',
  payload: Uint8Array = Buffer.from('authority-state')
): ZLinkAuthoritySnapshot {
  return { kind: 'snapshot', storeVersion: { value: 'version-a' } as never,
    payload, objectGeneration: 1n,
    authorityOwnerGeneration: 1n, ownerId: 'owner-source', ownerLeaseGeneration: 7n,
    allocation: { state: 'active', objectKind, stableType,
      descriptor: { meshName: 'mesh-a', rid: 'node-source' as never },
      descriptorLifecycleGeneration: 11n,
      capacity: objectKind === 'actor' ? { actors: 1, spots: 0 } : { actors: 0, spots: 1 } },
    storeNow: new Date(1) };
}

function foundBlob(bytes: Uint8Array) {
  const storeNow = new Date();
  return {
    kind: 'found' as const,
    bytes,
    expiresAt: new Date(storeNow.getTime() + 60_000),
    storeNow
  };
}
