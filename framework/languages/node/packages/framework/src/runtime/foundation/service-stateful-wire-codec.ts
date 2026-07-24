import type {
  ServiceActorRef,
  ServiceSessionBinding,
  ServiceSpotRef
} from './service-stateful-registry';
import { ServiceWireProtocolError } from './service-wire-m6a-codec';

const PREFIX_SIZE = 5;
const MAGIC_0 = 0x5a;
const MAGIC_1 = 0x4d;
const MAJOR = 1;

export const M6bServiceWireCommand = Object.freeze({
  spotSend: 21,
  spotRequest: 22,
  logicalMulticast: 23,
  actorSend: 24,
  actorRequest: 25,
  actorLookup: 26,
  actorDestroy: 27,
  actorJoin: 28,
  actorLeft: 29,
  boundSessionSend: 36,
  actorJoined: 37,
  boundSessionBind: 38,
  instanceSpot: 39,
  userSpotCreate: 47,
  userSpotClose: 48
});

export const M6bServiceWireFlag = Object.freeze({
  metadata: 1,
  boundSession: 2,
  sourceSpotRid: 4
});

export interface ServiceSpotRouteFence {
  readonly spot: ServiceSpotRef;
  readonly targetNodeRid: string;
  readonly targetNodeGeneration: bigint;
  readonly authorityOwnerGeneration: bigint;
}

export interface ServiceActorRouteFence {
  readonly actor: ServiceActorRef;
  readonly targetNodeGeneration: bigint;
  readonly authorityOwnerGeneration: bigint;
}

export interface ServiceBoundSessionSource {
  readonly sessionRid: string;
  readonly bindingGeneration: bigint;
  readonly sequence: bigint;
}

export interface ServiceInstanceRouteFence {
  readonly targetNodeRid: string;
  readonly targetNodeGeneration: bigint;
  readonly targetSpotRid: string;
  readonly objectGeneration: bigint;
  readonly ownerId: string;
  readonly authorityOwnerGeneration: bigint;
  readonly leaseGeneration: bigint;
  readonly storeVersion: string;
}

export interface ServiceInstanceActivationTarget {
  readonly targetNodeRid: string;
  readonly targetNodeGeneration: bigint;
  readonly targetSpotRid: string;
  readonly stableType: string;
  readonly descriptorVersion: string;
}

export interface ServiceUserSpotReservationFence {
  readonly reservationId: string;
  readonly expectedStoreVersion: string;
  readonly objectGeneration: bigint;
  readonly authorityOwnerGeneration: bigint;
  readonly targetNodeRid: string;
  readonly targetNodeGeneration: bigint;
  readonly targetOwnerId: string;
  readonly targetOwnerLeaseGeneration: bigint;
  readonly pendingCapacityDelta: number;
}

export interface ServiceUserSpotCreateRecord {
  readonly kind: 'userSpotCreate';
  readonly correlation: bigint;
  readonly operation: { readonly high: bigint; readonly low: bigint };
  readonly sourceNodeRid: string;
  readonly sourceNodeGeneration: bigint;
  readonly spotRid: string;
  readonly stableType: string;
  readonly reservation: ServiceUserSpotReservationFence;
  readonly deadlineUnixMs: bigint;
}

export interface ServiceUserSpotCloseRecord {
  readonly kind: 'userSpotClose';
  readonly correlation: bigint;
  readonly operation: { readonly high: bigint; readonly low: bigint };
  readonly sourceNodeRid: string;
  readonly sourceNodeGeneration: bigint;
  readonly target: {
    readonly spotRid: string;
    readonly objectGeneration: bigint;
    readonly targetNodeRid: string;
    readonly targetNodeGeneration: bigint;
    readonly authorityOwnerGeneration: bigint;
    readonly expectedStoreVersion: string;
  };
  readonly deadlineUnixMs: bigint;
}

export type ServiceBoundSessionTransition =
  | { readonly state: 'active'; readonly generation: bigint }
  | { readonly state: 'tombstone'; readonly retiredGeneration: bigint };

export type ServiceStatefulWireRecord =
  | {
      readonly kind: 'spotSend' | 'spotRequest';
      readonly correlation?: bigint;
      readonly sourceSpotRid: string;
      readonly target: ServiceSpotRouteFence;
    }
  | {
      readonly kind: 'logicalMulticast';
      readonly channelName: string;
      readonly topic: string;
      readonly sourceSpotRid: string;
    }
  | {
      readonly kind: 'actorSend' | 'actorRequest';
      readonly correlation?: bigint;
      readonly sourceActor?: ServiceActorRef;
      readonly target: ServiceActorRouteFence;
      readonly boundSession?: ServiceBoundSessionSource;
    }
  | {
      readonly kind: 'actorLookup';
      readonly correlation: bigint;
      readonly actorId: string;
    }
  | {
      readonly kind: 'actorDestroy';
      readonly correlation: bigint;
      readonly actor: ServiceActorRouteFence;
    }
  | {
      readonly kind: 'actorJoin';
      readonly correlation: bigint;
      readonly actor: ServiceActorRouteFence;
      readonly entry: boolean;
      readonly target: ServiceSpotRouteFence;
    }
  | {
      readonly kind: 'boundSessionSend';
      readonly actor: ServiceActorRouteFence;
      readonly expectedBindingGeneration: bigint;
    }
  | {
      readonly kind: 'boundSessionBind';
      readonly correlation: bigint;
      readonly actor: ServiceActorRouteFence;
      readonly sessionRid: string;
      readonly binding: ServiceBoundSessionTransition;
    }
  | {
      readonly kind: 'instanceSpot';
      readonly activation: 'ready';
      readonly route: ServiceInstanceRouteFence;
      readonly sourceNodeGeneration: bigint;
      readonly sourceNodeRid: string;
      readonly sourceSpotRid?: string;
      readonly operationKind: 'send' | 'request';
      readonly operation: { readonly high: bigint; readonly low: bigint };
      readonly replyRouteId?: bigint;
    }
  | {
      readonly kind: 'instanceSpot';
      readonly activation: 'missing';
      readonly target: ServiceInstanceActivationTarget;
      readonly sourceNodeGeneration: bigint;
      readonly sourceNodeRid: string;
      readonly sourceSpotRid?: string;
      readonly operationKind: 'send' | 'request';
      readonly operation: { readonly high: bigint; readonly low: bigint };
      readonly deadlineUnixMs: bigint;
      readonly replyRouteId?: bigint;
    }
  | ServiceUserSpotCreateRecord
  | ServiceUserSpotCloseRecord;

export type ServiceStatefulReplyTail =
  | {
      readonly kind: 'actorLookup';
      readonly actor: ServiceActorRef;
      readonly spot: ServiceSpotRef;
      readonly membershipEpoch: bigint;
      readonly authorityOwnerGeneration: bigint;
    }
  | {
      readonly kind: 'actorJoin';
      readonly joinResult: 0 | 1;
      readonly spot?: ServiceSpotRef;
      readonly membershipEpoch?: bigint;
    }
  | {
      readonly kind: 'streamBind';
      readonly bindingGeneration: bigint;
      readonly authorityOwnerGeneration: bigint;
    }
  | {
      readonly kind: 'userSpotCreate';
      readonly createResult: 'existing' | 'created' | 'rejected';
      readonly spotRid: string;
      readonly objectGeneration: bigint;
    }
  | {
      readonly kind: 'userSpotClose';
      readonly closed: boolean;
    };

export interface ServiceStatefulReply {
  readonly correlation: bigint;
  readonly terminalResult: number;
  readonly failureCode: number;
  readonly tail?: ServiceStatefulReplyTail;
}

export function encodeSpotHeader(
  kind: 'spotSend' | 'spotRequest',
  sourceSpotRid: string,
  target: ServiceSpotRouteFence,
  correlation?: bigint
): Buffer {
  return concat(
    prefix(kind === 'spotSend'
      ? M6bServiceWireCommand.spotSend
      : M6bServiceWireCommand.spotRequest),
    ...(kind === 'spotRequest' ? [u64(requirePositive(correlation, 'correlation'))] : []),
    rid(sourceSpotRid, 'sourceSpotRid'),
    spotFence(target)
  );
}

export function encodeActorHeader(
  kind: 'actorSend' | 'actorRequest',
  target: ServiceActorRouteFence,
  correlation?: bigint,
  sourceActor?: ServiceActorRef,
  boundSession?: ServiceBoundSessionSource
): Buffer {
  const flags = boundSession === undefined
    ? 0
    : M6bServiceWireFlag.boundSession | M6bServiceWireFlag.sourceSpotRid;
  return concat(
    prefix(
      kind === 'actorSend'
        ? M6bServiceWireCommand.actorSend
        : M6bServiceWireCommand.actorRequest,
      flags
    ),
    ...(kind === 'actorRequest' ? [u64(requirePositive(correlation, 'correlation'))] : []),
    optionalActor(sourceActor),
    actorFence(target),
    ...(boundSession === undefined
      ? []
      : [
          rid(boundSession.sessionRid, 'sourceSessionRid'),
          u64(boundSession.bindingGeneration),
          u64(boundSession.sequence)
        ])
  );
}

export function encodeLogicalMulticastHeader(
  channelName: string,
  topic: string,
  sourceSpotRid: string
): Buffer {
  return concat(
    prefix(M6bServiceWireCommand.logicalMulticast),
    text8(channelName, 'channelName'),
    text8(topic, 'topic'),
    rid(sourceSpotRid, 'sourceSpotRid')
  );
}

export function encodeActorLookupHeader(correlation: bigint, actorId: string): Buffer {
  return concat(
    prefix(M6bServiceWireCommand.actorLookup),
    u64(correlation),
    text8(actorId, 'actorId')
  );
}

export function encodeActorDestroyHeader(
  correlation: bigint,
  actor: ServiceActorRouteFence
): Buffer {
  return concat(prefix(M6bServiceWireCommand.actorDestroy), u64(correlation), actorFence(actor));
}

export function encodeActorJoinHeader(
  correlation: bigint,
  actor: ServiceActorRouteFence,
  entry: boolean,
  target: ServiceSpotRouteFence
): Buffer {
  return concat(
    prefix(M6bServiceWireCommand.actorJoin),
    u64(correlation),
    actorFence(actor),
    Buffer.of(entry ? 1 : 0),
    spotFence(target)
  );
}

export function encodeBoundSessionSendHeader(
  actor: ServiceActorRouteFence,
  expectedBindingGeneration: bigint
): Buffer {
  return concat(
    prefix(M6bServiceWireCommand.boundSessionSend),
    actorFence(actor),
    u64(expectedBindingGeneration)
  );
}

export function encodeBoundSessionBindHeader(
  correlation: bigint,
  actor: ServiceActorRouteFence,
  sessionRid: string,
  binding: ServiceBoundSessionTransition
): Buffer {
  const body = binding.state === 'active'
    ? u64(binding.generation)
    : u64(binding.retiredGeneration);
  return concat(
    prefix(M6bServiceWireCommand.boundSessionBind),
    u64(correlation),
    actorFence(actor),
    rid(sessionRid, 'sessionRid'),
    Buffer.of(binding.state === 'active' ? 1 : 2),
    u16(body.byteLength),
    body
  );
}

export function encodeInstanceSpotHeader(
  route: ServiceInstanceRouteFence,
  sourceNodeGeneration: bigint,
  sourceNodeRid: string,
  sourceSpotRid: string | undefined,
  operationKind: 'send' | 'request',
  operation: { readonly high: bigint; readonly low: bigint },
  replyRouteId?: bigint,
  hasMetadata = false
): Buffer {
  const routeBody = concat(
    rid(route.targetNodeRid, 'targetNodeRid'),
    u64(route.targetNodeGeneration),
    rid(route.targetSpotRid, 'targetSpotRid'),
    u64(route.objectGeneration),
    text8(route.ownerId, 'ownerId'),
    u64(route.authorityOwnerGeneration),
    u64(route.leaseGeneration),
    text16(route.storeVersion, 'storeVersion')
  );
  if (
    operationKind === 'send'
    && (operation.high !== 0n || operation.low !== 0n || replyRouteId !== undefined)
  ) {
    throw new RangeError('Instance Spot send must not carry an operation or reply route.');
  }
  if (operationKind === 'request' && operation.high === 0n && operation.low === 0n) {
    throw new RangeError('Instance Spot request requires a non-zero operation.');
  }
  return concat(
    prefix(
      M6bServiceWireCommand.instanceSpot,
      hasMetadata ? M6bServiceWireFlag.metadata : 0
    ),
    Buffer.of(1),
    u16(routeBody.byteLength),
    routeBody,
    u64Any(sourceNodeGeneration),
    rid(sourceNodeRid, 'sourceNodeRid'),
    optionalRid(sourceSpotRid),
    Buffer.of(operationKind === 'send' ? 1 : 2),
    u64Any(operation.high),
    u64Any(operation.low),
    ...(operationKind === 'request'
      ? [u64(requirePositive(replyRouteId, 'replyRouteId'))]
      : [])
  );
}

export function encodeInstanceSpotActivationHeader(
  target: ServiceInstanceActivationTarget,
  sourceNodeGeneration: bigint,
  sourceNodeRid: string,
  sourceSpotRid: string | undefined,
  operationKind: 'send' | 'request',
  operation: { readonly high: bigint; readonly low: bigint },
  deadlineUnixMs: bigint,
  replyRouteId?: bigint,
  hasMetadata = false
): Buffer {
  if (operation.high === 0n && operation.low === 0n) {
    throw new RangeError('Instance Spot activation requires a non-zero operation identity.');
  }
  if (deadlineUnixMs <= 0n) {
    throw new RangeError('Instance Spot activation requires a positive deadline.');
  }
  if (operationKind === 'send' && replyRouteId !== undefined) {
    throw new RangeError('Instance Spot send must not carry a reply route.');
  }
  const targetBody = concat(
    rid(target.targetNodeRid, 'targetNodeRid'),
    u64(target.targetNodeGeneration),
    rid(target.targetSpotRid, 'targetSpotRid'),
    text16(target.stableType, 'stableType'),
    text16(target.descriptorVersion, 'descriptorVersion')
  );
  return concat(
    prefix(
      M6bServiceWireCommand.instanceSpot,
      hasMetadata ? M6bServiceWireFlag.metadata : 0
    ),
    Buffer.of(2),
    u16(targetBody.byteLength),
    targetBody,
    u64Any(sourceNodeGeneration),
    rid(sourceNodeRid, 'sourceNodeRid'),
    optionalRid(sourceSpotRid),
    Buffer.of(operationKind === 'send' ? 1 : 2),
    u64Any(operation.high),
    u64Any(operation.low),
    u64(deadlineUnixMs),
    ...(operationKind === 'request'
      ? [u64(requirePositive(replyRouteId, 'replyRouteId'))]
      : [])
  );
}

export function encodeUserSpotCreateHeader(record: Omit<ServiceUserSpotCreateRecord, 'kind'>): Buffer {
  if (record.operation.high === 0n && record.operation.low === 0n) {
    throw new RangeError('User Spot create requires a non-zero operation identity.');
  }
  const fence = record.reservation;
  if (fence.pendingCapacityDelta < 1) {
    throw new RangeError('User Spot create requires a positive pending capacity delta.');
  }
  return concat(
    prefix(M6bServiceWireCommand.userSpotCreate),
    u64(record.correlation),
    u64Any(record.operation.high),
    u64Any(record.operation.low),
    rid(record.sourceNodeRid, 'sourceNodeRid'),
    u64(record.sourceNodeGeneration),
    rid(record.spotRid, 'spotRid'),
    text8(record.stableType, 'stableType'),
    text8(fence.reservationId, 'reservationId'),
    text16(fence.expectedStoreVersion, 'expectedStoreVersion'),
    u64(fence.objectGeneration),
    u64(fence.authorityOwnerGeneration),
    rid(fence.targetNodeRid, 'targetNodeRid'),
    u64(fence.targetNodeGeneration),
    text8(fence.targetOwnerId, 'targetOwnerId'),
    u64(fence.targetOwnerLeaseGeneration),
    u32(fence.pendingCapacityDelta, 'pendingCapacityDelta'),
    u64(record.deadlineUnixMs)
  );
}

export function encodeUserSpotCloseHeader(record: Omit<ServiceUserSpotCloseRecord, 'kind'>): Buffer {
  if (record.operation.high === 0n && record.operation.low === 0n) {
    throw new RangeError('User Spot close requires a non-zero operation identity.');
  }
  const fence = concat(
    rid(record.target.spotRid, 'spotRid'),
    u64(record.target.objectGeneration),
    rid(record.target.targetNodeRid, 'targetNodeRid'),
    u64(record.target.targetNodeGeneration),
    u64(record.target.authorityOwnerGeneration),
    text16(record.target.expectedStoreVersion, 'expectedStoreVersion')
  );
  return concat(
    prefix(M6bServiceWireCommand.userSpotClose),
    u64(record.correlation),
    u64Any(record.operation.high),
    u64Any(record.operation.low),
    rid(record.sourceNodeRid, 'sourceNodeRid'),
    u64(record.sourceNodeGeneration),
    Buffer.of(1),
    u16(fence.byteLength),
    fence,
    u64(record.deadlineUnixMs)
  );
}

export function decodeStatefulHeader(frame: Uint8Array): ServiceStatefulWireRecord {
  const reader = new Reader(frame);
  const command = reader.prefix();
  switch (command.command) {
    case M6bServiceWireCommand.spotSend:
    case M6bServiceWireCommand.spotRequest: {
      requireFlags(command.flags, 0);
      const request = command.command === M6bServiceWireCommand.spotRequest;
      const correlation = request ? reader.nonZeroU64('correlation') : undefined;
      const sourceSpotRid = reader.rid('sourceSpotRid');
      const target = reader.spotFence();
      reader.end();
      return {
        kind: request ? 'spotRequest' : 'spotSend',
        ...(correlation === undefined ? {} : { correlation }),
        sourceSpotRid,
        target
      };
    }
    case M6bServiceWireCommand.actorSend:
    case M6bServiceWireCommand.actorRequest: {
      const request = command.command === M6bServiceWireCommand.actorRequest;
      const hasBinding = command.flags !== 0;
      requireFlags(
        command.flags,
        hasBinding
          ? M6bServiceWireFlag.boundSession | M6bServiceWireFlag.sourceSpotRid
          : 0
      );
      const correlation = request ? reader.nonZeroU64('correlation') : undefined;
      const sourceActor = reader.optionalActor();
      const target = reader.actorFence();
      const boundSession = hasBinding
        ? {
            sessionRid: reader.rid('sourceSessionRid'),
            bindingGeneration: reader.nonZeroU64('sourceBindingGeneration'),
            sequence: reader.nonZeroU64('sourceSessionSequence')
          }
        : undefined;
      reader.end();
      return {
        kind: request ? 'actorRequest' : 'actorSend',
        ...(correlation === undefined ? {} : { correlation }),
        ...(sourceActor === undefined ? {} : { sourceActor }),
        target,
        ...(boundSession === undefined ? {} : { boundSession })
      };
    }
    case M6bServiceWireCommand.logicalMulticast: {
      requireFlags(command.flags, 0);
      const result = {
        kind: 'logicalMulticast' as const,
        channelName: reader.text8('channelName'),
        topic: reader.text8('topic'),
        sourceSpotRid: reader.rid('sourceSpotRid')
      };
      reader.end();
      return result;
    }
    case M6bServiceWireCommand.actorLookup: {
      requireFlags(command.flags, 0);
      const result = {
        kind: 'actorLookup' as const,
        correlation: reader.nonZeroU64('correlation'),
        actorId: reader.text8('actorId')
      };
      reader.end();
      return result;
    }
    case M6bServiceWireCommand.actorDestroy: {
      requireFlags(command.flags, 0);
      const result = {
        kind: 'actorDestroy' as const,
        correlation: reader.nonZeroU64('correlation'),
        actor: reader.actorFence()
      };
      reader.end();
      return result;
    }
    case M6bServiceWireCommand.actorJoin: {
      requireFlags(command.flags, 0);
      const correlation = reader.nonZeroU64('correlation');
      const actor = reader.actorFence();
      const entry = reader.bool8('entry');
      const target = reader.spotFence();
      reader.end();
      return { kind: 'actorJoin', correlation, actor, entry, target };
    }
    case M6bServiceWireCommand.boundSessionSend: {
      requireFlags(command.flags, 0);
      const actor = reader.actorFence();
      const expectedBindingGeneration = reader.nonZeroU64('expectedBindingGeneration');
      reader.end();
      return { kind: 'boundSessionSend', actor, expectedBindingGeneration };
    }
    case M6bServiceWireCommand.boundSessionBind: {
      requireFlags(command.flags, 0);
      const correlation = reader.nonZeroU64('correlation');
      const actor = reader.actorFence();
      const sessionRid = reader.rid('sessionRid');
      const state = reader.u8('bindingState');
      const bodyLength = reader.u16('bindingBodyLength');
      if (bodyLength !== 8) fail('Binding transition body must contain one u64.');
      const generation = reader.nonZeroU64('bindingGeneration');
      reader.end();
      if (state !== 1 && state !== 2) fail('Unknown binding transition state.');
      return {
        kind: 'boundSessionBind',
        correlation,
        actor,
        sessionRid,
        binding: state === 1
          ? { state: 'active', generation }
          : { state: 'tombstone', retiredGeneration: generation }
      };
    }
    case M6bServiceWireCommand.instanceSpot: {
      if ((command.flags & ~M6bServiceWireFlag.metadata) !== 0) {
        fail(`Invalid command flags '${command.flags}'.`);
      }
      const version = reader.u8('instanceRoute.version');
      if (version !== 1 && version !== 2) fail('Unsupported Instance route version.');
      const routeLength = reader.u16('instanceRoute.length');
      const routeEnd = reader.offset + routeLength;
      const commonTarget = {
        targetNodeRid: reader.rid('targetNodeRid'),
        targetNodeGeneration: reader.nonZeroU64('targetNodeGeneration'),
        targetSpotRid: reader.rid('targetSpotRid')
      };
      const route: ServiceInstanceRouteFence | undefined = version === 1
        ? {
            ...commonTarget,
            objectGeneration: reader.nonZeroU64('objectGeneration'),
            ownerId: reader.text8('ownerId'),
            authorityOwnerGeneration: reader.nonZeroU64('authorityOwnerGeneration'),
            leaseGeneration: reader.nonZeroU64('leaseGeneration'),
            storeVersion: reader.text16('storeVersion')
          }
        : undefined;
      const target: ServiceInstanceActivationTarget | undefined = version === 2
        ? {
            ...commonTarget,
            stableType: reader.text16('stableType'),
            descriptorVersion: reader.text16('descriptorVersion')
          }
        : undefined;
      if (reader.offset !== routeEnd) fail('Invalid Instance route body length.');
      const sourceNodeGeneration = reader.nonZeroU64('sourceNodeGeneration');
      const sourceNodeRid = reader.rid('sourceNodeRid');
      const sourceSpotRid = reader.optionalRid('sourceSpotRid');
      const operationValue = reader.u8('operationKind');
      if (operationValue !== 1 && operationValue !== 2) fail('Unknown Instance operation kind.');
      const operation = {
        high: reader.u64('operation.high'),
        low: reader.u64('operation.low')
      };
      const operationKind = operationValue === 1 ? 'send' as const : 'request' as const;
      if (version === 1 && (
        (operationKind === 'send' && (operation.high !== 0n || operation.low !== 0n))
        || (operationKind === 'request' && operation.high === 0n && operation.low === 0n)
      )) {
        fail('Invalid Instance operation identity.');
      }
      if (version === 2 && operation.high === 0n && operation.low === 0n) {
        fail('Instance activation requires a non-zero operation identity.');
      }
      const deadlineUnixMs = version === 2
        ? reader.nonZeroU64('deadlineUnixMs')
        : undefined;
      const replyRouteId = operationKind === 'request'
        ? reader.nonZeroU64('replyRouteId')
        : undefined;
      reader.end();
      const common = {
        kind: 'instanceSpot' as const,
        sourceNodeGeneration,
        sourceNodeRid,
        ...(sourceSpotRid === undefined ? {} : { sourceSpotRid }),
        operationKind,
        operation,
        ...(replyRouteId === undefined ? {} : { replyRouteId })
      };
      return version === 1
        ? { ...common, activation: 'ready', route: route! }
        : {
            ...common,
            activation: 'missing',
            target: target!,
            deadlineUnixMs: deadlineUnixMs!
          };
    }
    case M6bServiceWireCommand.userSpotCreate: {
      requireFlags(command.flags, 0);
      const correlation = reader.nonZeroU64('correlation');
      const operation = {
        high: reader.u64('operation.high'),
        low: reader.u64('operation.low')
      };
      if (operation.high === 0n && operation.low === 0n) {
        fail('User Spot create requires a non-zero operation identity.');
      }
      const record: ServiceUserSpotCreateRecord = {
        kind: 'userSpotCreate',
        correlation,
        operation,
        sourceNodeRid: reader.rid('sourceNodeRid'),
        sourceNodeGeneration: reader.nonZeroU64('sourceNodeGeneration'),
        spotRid: reader.rid('spotRid'),
        stableType: reader.text8('stableType'),
        reservation: {
          reservationId: reader.text8('reservationId'),
          expectedStoreVersion: reader.text16('expectedStoreVersion'),
          objectGeneration: reader.nonZeroU64('objectGeneration'),
          authorityOwnerGeneration: reader.nonZeroU64('authorityOwnerGeneration'),
          targetNodeRid: reader.rid('targetNodeRid'),
          targetNodeGeneration: reader.nonZeroU64('targetNodeGeneration'),
          targetOwnerId: reader.text8('targetOwnerId'),
          targetOwnerLeaseGeneration: reader.nonZeroU64('targetOwnerLeaseGeneration'),
          pendingCapacityDelta: reader.u32('pendingCapacityDelta')
        },
        deadlineUnixMs: reader.nonZeroU64('deadlineUnixMs')
      };
      if (record.reservation.pendingCapacityDelta === 0) {
        fail('User Spot create requires a positive pending capacity delta.');
      }
      reader.end();
      return record;
    }
    case M6bServiceWireCommand.userSpotClose: {
      requireFlags(command.flags, 0);
      const correlation = reader.nonZeroU64('correlation');
      const operation = {
        high: reader.u64('operation.high'),
        low: reader.u64('operation.low')
      };
      if (operation.high === 0n && operation.low === 0n) {
        fail('User Spot close requires a non-zero operation identity.');
      }
      const sourceNodeRid = reader.rid('sourceNodeRid');
      const sourceNodeGeneration = reader.nonZeroU64('sourceNodeGeneration');
      if (reader.u8('closeFence.version') !== 1) fail('Unsupported User Spot close fence version.');
      const fenceLength = reader.u16('closeFence.length');
      const fenceEnd = reader.offset + fenceLength;
      if (fenceEnd > reader.bytes.byteLength) fail('Truncated User Spot close fence.');
      const target = {
        spotRid: reader.rid('spotRid'),
        objectGeneration: reader.nonZeroU64('objectGeneration'),
        targetNodeRid: reader.rid('targetNodeRid'),
        targetNodeGeneration: reader.nonZeroU64('targetNodeGeneration'),
        authorityOwnerGeneration: reader.nonZeroU64('authorityOwnerGeneration'),
        expectedStoreVersion: reader.text16('expectedStoreVersion')
      };
      if (reader.offset !== fenceEnd) fail('Invalid User Spot close fence length.');
      const deadlineUnixMs = reader.nonZeroU64('deadlineUnixMs');
      reader.end();
      return {
        kind: 'userSpotClose',
        correlation,
        operation,
        sourceNodeRid,
        sourceNodeGeneration,
        target,
        deadlineUnixMs
      };
    }
    default:
      fail(`Command '${command.command}' is not owned by the M6B codec.`);
  }
}

export function encodeStatefulReply(
  correlation: bigint,
  terminalResult: number,
  failureCode: number,
  tail?: ServiceStatefulReplyTail
): Buffer {
  const tailBytes = tail === undefined ? Buffer.alloc(0) : encodeReplyTail(tail);
  return concat(
    prefix(20),
    u64(correlation),
    u32(terminalResult, 'terminalResult'),
    u32(failureCode, 'failureCode'),
    tailBytes
  );
}

export function decodeStatefulReply(
  frame: Uint8Array,
  expectedCorrelation: bigint,
  operationKind: 'spotRequest' | 'actorRequest' | 'actorLookup' | 'actorDestroy' | 'actorJoin' | 'streamBind'
    | 'instanceSpotRequest' | 'userSpotCreate' | 'userSpotClose'
): ServiceStatefulReply {
  const reader = new Reader(frame);
  const command = reader.prefix();
  if (command.command !== 20 || command.flags !== 0) fail('Invalid stateful reply prefix.');
  const correlation = reader.nonZeroU64('correlation');
  if (correlation !== expectedCorrelation) fail('Stateful reply correlation mismatch.');
  const terminalResult = reader.u32('terminalResult');
  const failureCode = reader.u32('failureCode');
  let tail: ServiceStatefulReplyTail | undefined;
  if (terminalResult === 0) {
    if (operationKind === 'actorLookup') {
      tail = {
        kind: 'actorLookup',
        actor: reader.actorRef('actor'),
        spot: {
          spotRid: reader.rid('spotRid'),
          generation: reader.nonZeroU64('spotGeneration')
        },
        membershipEpoch: reader.nonZeroU64('membershipEpoch'),
        authorityOwnerGeneration: reader.nonZeroU64('authorityOwnerGeneration')
      };
    } else if (operationKind === 'actorJoin') {
      const joinResult = reader.u32('joinResult');
      if (joinResult !== 0 && joinResult !== 1) fail('Invalid actor join result.');
      const bodyLength = reader.u16('joinBodyLength');
      const bodyEnd = reader.offset + bodyLength;
      let spot: ServiceSpotRef | undefined;
      if (joinResult === 0) {
        spot = reader.spotRef();
        const membershipEpoch = reader.nonZeroU64('membershipEpoch');
        if (reader.offset !== bodyEnd) fail('Invalid actor join body length.');
        tail = { kind: 'actorJoin', joinResult, spot, membershipEpoch };
      } else {
        const hasSpot = reader.bool8('hasSpot');
        const optionalLength = reader.u16('optionalSpotLength');
        const optionalEnd = reader.offset + optionalLength;
        if (hasSpot) spot = reader.spotRef();
        if (reader.offset !== optionalEnd) fail('Invalid optional Spot body length.');
        if (reader.offset !== bodyEnd) fail('Invalid actor join body length.');
        tail = { kind: 'actorJoin', joinResult, ...(spot === undefined ? {} : { spot }) };
      }
    } else if (operationKind === 'streamBind') {
      tail = {
        kind: 'streamBind',
        bindingGeneration: reader.nonZeroU64('bindingGeneration'),
        authorityOwnerGeneration: reader.nonZeroU64('authorityOwnerGeneration')
      };
    } else if (operationKind === 'userSpotCreate') {
      const createResult = reader.u8('createResult');
      if (createResult < 1 || createResult > 3) fail('Invalid User Spot create result.');
      tail = {
        kind: 'userSpotCreate',
        createResult: (['existing', 'created', 'rejected'] as const)[createResult - 1]!,
        spotRid: reader.rid('spotRid'),
        objectGeneration: reader.nonZeroU64('objectGeneration')
      };
    } else if (operationKind === 'userSpotClose') {
      tail = {
        kind: 'userSpotClose',
        closed: reader.bool8('closed')
      };
    }
  }
  reader.end();
  return {
    correlation,
    terminalResult,
    failureCode,
    ...(tail === undefined ? {} : { tail })
  };
}

export function sessionBindingFromWire(
  actor: ServiceActorRef,
  sessionRid: string,
  sessionOwnerNodeRid: string,
  generation: bigint,
  membershipEpoch: bigint
): ServiceSessionBinding {
  return {
    actor,
    sessionRid,
    sessionOwnerNodeRid,
    bindingGeneration: generation,
    membershipEpoch
  };
}

function encodeReplyTail(tail: ServiceStatefulReplyTail): Buffer {
  switch (tail.kind) {
    case 'actorLookup':
      return concat(
        actorRef(tail.actor),
        rid(tail.spot.spotRid, 'spotRid'),
        u64(tail.spot.generation),
        u64(tail.membershipEpoch),
        u64(tail.authorityOwnerGeneration)
      );
    case 'actorJoin': {
      if (tail.joinResult === 0) {
        if (tail.spot === undefined) fail('Accepted actor join requires a SpotRef.');
        if (tail.membershipEpoch === undefined) {
          fail('Accepted actor join requires a membership epoch.');
        }
        const body = concat(spotRef(tail.spot), u64(tail.membershipEpoch));
        return concat(u32(0, 'joinResult'), u16(body.byteLength), body);
      }
      const optional = tail.spot === undefined
        ? concat(Buffer.of(0), u16(0))
        : (() => {
            const body = spotRef(tail.spot);
            return concat(Buffer.of(1), u16(body.byteLength), body);
          })();
      return concat(u32(1, 'joinResult'), u16(optional.byteLength), optional);
    }
    case 'streamBind':
      return concat(u64(tail.bindingGeneration), u64(tail.authorityOwnerGeneration));
    case 'userSpotCreate':
      return concat(
        Buffer.of((['existing', 'created', 'rejected'] as const).indexOf(tail.createResult) + 1),
        rid(tail.spotRid, 'spotRid'),
        u64(tail.objectGeneration)
      );
    case 'userSpotClose':
      return Buffer.of(tail.closed ? 1 : 0);
  }
}

function prefix(command: number, flags = 0): Buffer {
  return Buffer.from([MAGIC_0, MAGIC_1, MAJOR, command, flags]);
}

function actorFence(value: ServiceActorRouteFence): Buffer {
  return concat(
    actorRef(value.actor),
    rid(value.actor.nodeRid, 'targetNodeRid'),
    u64(value.targetNodeGeneration),
    u64(value.authorityOwnerGeneration)
  );
}

function spotFence(value: ServiceSpotRouteFence): Buffer {
  return concat(
    spotRef(value.spot),
    rid(value.targetNodeRid, 'targetNodeRid'),
    u64(value.targetNodeGeneration),
    u64(value.authorityOwnerGeneration)
  );
}

function actorRef(value: ServiceActorRef): Buffer {
  return concat(text8(value.actorId, 'actorId'), u64(value.generation));
}

function optionalActor(value: ServiceActorRef | undefined): Buffer {
  return value === undefined
    ? Buffer.of(0)
    : concat(text8(value.actorId, 'sourceActorId'), u64(value.generation));
}

function spotRef(value: ServiceSpotRef): Buffer {
  return concat(rid(value.spotRid, 'spotRid'), u64(value.generation));
}

function rid(value: string, name: string): Buffer {
  return sized8(value, name);
}

function text8(value: string, name: string): Buffer {
  return sized8(value, name);
}

function text16(value: string, name: string): Buffer {
  const bytes = Buffer.from(value);
  if (bytes.byteLength < 1 || bytes.byteLength > 0xffff || bytes.includes(0)) {
    throw new RangeError(`${name} must be 1..65535 UTF-8 bytes without NUL.`);
  }
  return concat(u16(bytes.byteLength), bytes);
}

function optionalRid(value: string | undefined): Buffer {
  return value === undefined ? Buffer.of(0) : rid(value, 'sourceSpotRid');
}

function sized8(value: string, name: string): Buffer {
  const bytes = Buffer.from(value);
  if (bytes.byteLength < 1 || bytes.byteLength > 255 || bytes.includes(0)) {
    throw new RangeError(`${name} must be 1..255 UTF-8 bytes without NUL.`);
  }
  return concat(Buffer.of(bytes.byteLength), bytes);
}

function u16(value: number): Buffer {
  if (!Number.isInteger(value) || value < 0 || value > 0xffff) fail('u16 value is out of range.');
  const result = Buffer.alloc(2);
  result.writeUInt16BE(value);
  return result;
}

function u32(value: number, name: string): Buffer {
  if (!Number.isInteger(value) || value < 0 || value > 0xffff_ffff) {
    throw new RangeError(`${name} must be a u32.`);
  }
  const result = Buffer.alloc(4);
  result.writeUInt32BE(value);
  return result;
}

function u64(value: bigint): Buffer {
  requirePositive(value, 'u64');
  const result = Buffer.alloc(8);
  result.writeBigUInt64BE(value);
  return result;
}

function u64Any(value: bigint): Buffer {
  if (value < 0n || value > 0xffff_ffff_ffff_ffffn) {
    throw new RangeError('u64 value is out of range.');
  }
  const result = Buffer.alloc(8);
  result.writeBigUInt64BE(value);
  return result;
}

function requirePositive(value: bigint | undefined, name: string): bigint {
  if (value === undefined || value < 1n || value > 0xffff_ffff_ffff_ffffn) {
    throw new RangeError(`${name} must be a non-zero u64.`);
  }
  return value;
}

function requireFlags(actual: number, expected: number): void {
  if (actual !== expected) fail(`Invalid command flags '${actual}'.`);
}

function concat(...parts: readonly Uint8Array[]): Buffer {
  return Buffer.concat(parts.map(part => Buffer.from(part)));
}

function fail(message: string): never {
  throw new ServiceWireProtocolError(message);
}

class Reader {
  readonly bytes: Buffer;
  offset = 0;

  constructor(value: Uint8Array) {
    this.bytes = Buffer.from(value);
  }

  get remaining(): number {
    return this.bytes.byteLength - this.offset;
  }

  prefix(): { readonly command: number; readonly flags: number } {
    this.need(PREFIX_SIZE, 'prefix');
    if (
      this.bytes[this.offset] !== MAGIC_0
      || this.bytes[this.offset + 1] !== MAGIC_1
      || this.bytes[this.offset + 2] !== MAJOR
    ) {
      fail('Invalid service wire prefix.');
    }
    const command = this.bytes[this.offset + 3]!;
    const flags = this.bytes[this.offset + 4]!;
    this.offset += PREFIX_SIZE;
    return { command, flags };
  }

  u8(name: string): number {
    this.need(1, name);
    return this.bytes[this.offset++]!;
  }

  bool8(name: string): boolean {
    const value = this.u8(name);
    if (value !== 0 && value !== 1) fail(`${name} must be bool8.`);
    return value === 1;
  }

  u16(name: string): number {
    this.need(2, name);
    const value = this.bytes.readUInt16BE(this.offset);
    this.offset += 2;
    return value;
  }

  u32(name: string): number {
    this.need(4, name);
    const value = this.bytes.readUInt32BE(this.offset);
    this.offset += 4;
    return value;
  }

  nonZeroU64(name: string): bigint {
    this.need(8, name);
    const value = this.bytes.readBigUInt64BE(this.offset);
    this.offset += 8;
    if (value === 0n) fail(`${name} must be a non-zero u64.`);
    return value;
  }

  u64(name: string): bigint {
    this.need(8, name);
    const value = this.bytes.readBigUInt64BE(this.offset);
    this.offset += 8;
    return value;
  }

  rid(name: string): string {
    return this.sized8(name);
  }

  text8(name: string): string {
    return this.sized8(name);
  }

  text16(name: string): string {
    const length = this.u16(`${name}.length`);
    if (length === 0) fail(`${name} must not be empty.`);
    this.need(length, name);
    const value = this.bytes.subarray(this.offset, this.offset + length).toString();
    this.offset += length;
    if (value.includes('\0')) fail(`${name} contains NUL.`);
    return value;
  }

  optionalRid(name: string): string | undefined {
    const length = this.u8(`${name}.length`);
    if (length === 0) return undefined;
    this.need(length, name);
    const value = this.bytes.subarray(this.offset, this.offset + length).toString();
    this.offset += length;
    if (value.includes('\0')) fail(`${name} contains NUL.`);
    return value;
  }

  actorRef(name: string): ServiceActorRef {
    return {
      nodeRid: '',
      actorId: this.text8(`${name}.actorId`),
      generation: this.nonZeroU64(`${name}.generation`)
    };
  }

  optionalActor(): ServiceActorRef | undefined {
    const length = this.u8('sourceActorId.length');
    if (length === 0) return undefined;
    this.need(length, 'sourceActorId');
    const actorId = this.bytes.subarray(this.offset, this.offset + length).toString();
    this.offset += length;
    if (actorId.includes('\0')) fail('sourceActorId contains NUL.');
    return {
      nodeRid: '',
      actorId,
      generation: this.nonZeroU64('sourceActorGeneration')
    };
  }

  spotRef(): ServiceSpotRef {
    return {
      spotRid: this.rid('spotRid'),
      generation: this.nonZeroU64('spotGeneration')
    };
  }

  actorFence(): ServiceActorRouteFence {
    const actor = this.actorRef('actor');
    const targetNodeRid = this.rid('targetNodeRid');
    return {
      actor: { ...actor, nodeRid: targetNodeRid },
      targetNodeGeneration: this.nonZeroU64('targetNodeGeneration'),
      authorityOwnerGeneration: this.nonZeroU64('authorityOwnerGeneration')
    };
  }

  spotFence(): ServiceSpotRouteFence {
    const spot = this.spotRef();
    return {
      spot,
      targetNodeRid: this.rid('targetNodeRid'),
      targetNodeGeneration: this.nonZeroU64('targetNodeGeneration'),
      authorityOwnerGeneration: this.nonZeroU64('authorityOwnerGeneration')
    };
  }

  end(): void {
    if (this.remaining !== 0) fail('Service wire record has trailing bytes.');
  }

  private sized8(name: string): string {
    const length = this.u8(`${name}.length`);
    if (length === 0) fail(`${name} must not be empty.`);
    this.need(length, name);
    const value = this.bytes.subarray(this.offset, this.offset + length).toString();
    this.offset += length;
    if (value.includes('\0')) fail(`${name} contains NUL.`);
    return value;
  }

  private need(count: number, name: string): void {
    if (count < 0 || count > this.remaining) fail(`Truncated ${name}.`);
  }
}
