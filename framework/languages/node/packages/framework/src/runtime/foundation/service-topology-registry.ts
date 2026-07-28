import { descriptorConnectionNotRequired } from './route-mesh-connection-policy';

export type ServiceNodeState =
  | 'preparing'
  | 'serving'
  | 'retiring'
  | 'draining'
  | 'stopped'
  | 'error';

export type ServiceObjectRole = 'none' | 'client' | 'server';

export interface ServiceChannelDescriptor {
  readonly name: string;
  readonly weight: number;
}

export interface ServiceNodeDescriptor {
  readonly meshName: string;
  readonly nodeRoutingId: string;
  readonly lifecycleGeneration: bigint;
  readonly descriptorRevision: bigint;
  readonly advertisedEndpoint: string;
  readonly channels: readonly ServiceChannelDescriptor[];
  readonly state: ServiceNodeState;
  readonly securityIdentity: string;
  readonly effectiveMaxMessageBytes: number;
  readonly applicationVersion: bigint;
  readonly protocolCapabilities: readonly string[];
  readonly objectRole: ServiceObjectRole;
  readonly placementWeight: number;
  readonly activeCapacityLimit: number;
  readonly pendingCapacityLimit: number;
  readonly activeCapacityUsed: number;
  readonly pendingCapacityUsed: number;
}

export interface AdmittedServicePeer {
  readonly descriptor: ServiceNodeDescriptor;
  readonly connectionId: string;
}

export type PeerAdmissionResult =
  | 'admitted'
  | 'notRequired'
  | 'meshMismatch'
  | 'invalidDescriptor'
  | 'staleDescriptor';

const REQUIRED_CAPABILITY = 'framework-service-v11';
const MAX_CAPACITY = 0x7fff_ffff;

/** Owns immutable peer snapshots and fences late physical-connection events. */
export class ServiceTopologyRegistry {
  private local: ServiceNodeDescriptor;
  private readonly peersByRid = new Map<string, AdmittedServicePeer>();
  private readonly notRequiredByRid = new Map<string, ServiceNodeDescriptor>();
  private readonly selectionCursor = new Map<string, bigint>();

  constructor(local: ServiceNodeDescriptor) {
    validateDescriptor(local);
    this.local = cloneDescriptor(local);
  }

  localDescriptor(): ServiceNodeDescriptor {
    return cloneDescriptor(this.local);
  }

  publishLocal(descriptor: ServiceNodeDescriptor): void {
    validateDescriptor(descriptor);
    if (
      descriptor.meshName !== this.local.meshName
      || descriptor.nodeRoutingId !== this.local.nodeRoutingId
      || descriptor.lifecycleGeneration !== this.local.lifecycleGeneration
    ) {
      throw new TypeError('Published descriptor changes the local node identity.');
    }
    if (descriptor.descriptorRevision <= this.local.descriptorRevision) {
      throw new RangeError('Published descriptor revision must increase.');
    }
    this.local = cloneDescriptor(descriptor);
    for (const [nodeRoutingId, remote] of this.notRequiredByRid) {
      if (!descriptorConnectionNotRequired(this.local, remote)) {
        this.notRequiredByRid.delete(nodeRoutingId);
      }
    }
  }

  admit(descriptor: ServiceNodeDescriptor, connectionId: string): PeerAdmissionResult {
    try {
      validateDescriptor(descriptor);
      requireText(connectionId, 'connectionId');
    } catch {
      return 'invalidDescriptor';
    }
    if (descriptor.meshName !== this.local.meshName) return 'meshMismatch';
    if (descriptor.nodeRoutingId === this.local.nodeRoutingId) return 'invalidDescriptor';
    if (descriptorConnectionNotRequired(this.local, descriptor)) {
      this.peersByRid.delete(descriptor.nodeRoutingId);
      this.notRequiredByRid.set(
        descriptor.nodeRoutingId,
        cloneDescriptor(descriptor)
      );
      return 'notRequired';
    }
    this.notRequiredByRid.delete(descriptor.nodeRoutingId);

    const current = this.peersByRid.get(descriptor.nodeRoutingId);
    if (
      current !== undefined
      && current.descriptor.lifecycleGeneration === descriptor.lifecycleGeneration
      && (
        current.descriptor.descriptorRevision > descriptor.descriptorRevision
        || (
          current.descriptor.descriptorRevision === descriptor.descriptorRevision
          && !sameDescriptor(current.descriptor, descriptor)
        )
      )
    ) {
      return 'staleDescriptor';
    }
    this.peersByRid.set(descriptor.nodeRoutingId, {
      descriptor: cloneDescriptor(descriptor),
      connectionId
    });
    return 'admitted';
  }

  disconnect(nodeRoutingId: string, connectionId: string): boolean {
    const current = this.peersByRid.get(nodeRoutingId);
    if (current === undefined || current.connectionId !== connectionId) return false;
    this.peersByRid.delete(nodeRoutingId);
    return true;
  }

  peer(nodeRoutingId: string): AdmittedServicePeer | undefined {
    const peer = this.peersByRid.get(nodeRoutingId);
    return peer === undefined ? undefined : clonePeer(peer);
  }

  peers(): readonly AdmittedServicePeer[] {
    return [...this.peersByRid.values()]
      .sort((left, right) => left.descriptor.nodeRoutingId.localeCompare(right.descriptor.nodeRoutingId))
      .map(clonePeer);
  }

  notRequiredPeers(): readonly ServiceNodeDescriptor[] {
    return [...this.notRequiredByRid.values()]
      .sort((left, right) => left.nodeRoutingId.localeCompare(right.nodeRoutingId))
      .map(cloneDescriptor);
  }

  replaceDiscoveredNotRequired(
    descriptors: readonly ServiceNodeDescriptor[]
  ): void {
    const next = new Map<string, ServiceNodeDescriptor>();
    for (const descriptor of descriptors) {
      validateDescriptor(descriptor);
      if (
        descriptor.meshName === this.local.meshName
        && descriptor.nodeRoutingId !== this.local.nodeRoutingId
        && descriptorConnectionNotRequired(this.local, descriptor)
      ) {
        next.set(descriptor.nodeRoutingId, cloneDescriptor(descriptor));
      }
    }
    this.notRequiredByRid.clear();
    for (const [nodeRoutingId, descriptor] of next) {
      this.notRequiredByRid.set(nodeRoutingId, descriptor);
    }
  }

  markNotRequired(descriptor: ServiceNodeDescriptor): void {
    validateDescriptor(descriptor);
    if (
      descriptor.meshName !== this.local.meshName
      || descriptor.nodeRoutingId === this.local.nodeRoutingId
      || !descriptorConnectionNotRequired(this.local, descriptor)
    ) {
      return;
    }
    this.peersByRid.delete(descriptor.nodeRoutingId);
    this.notRequiredByRid.set(
      descriptor.nodeRoutingId,
      cloneDescriptor(descriptor)
    );
  }

  forgetNotRequired(nodeRoutingId: string): void {
    this.notRequiredByRid.delete(nodeRoutingId);
  }

  knownDescriptor(nodeRoutingId: string): ServiceNodeDescriptor | undefined {
    const admitted = this.peersByRid.get(nodeRoutingId)?.descriptor;
    const descriptor = admitted ?? this.notRequiredByRid.get(nodeRoutingId);
    return descriptor === undefined ? undefined : cloneDescriptor(descriptor);
  }

  selectChannel(channelName: string): AdmittedServicePeer | undefined {
    requireText(channelName, 'channelName');
    const eligible = this.peers()
      .map(peer => ({ peer, channel: findChannel(peer.descriptor, channelName) }))
      .filter((value): value is {
        peer: AdmittedServicePeer;
        channel: ServiceChannelDescriptor;
      } => value.channel !== undefined && value.peer.descriptor.state === 'serving' && value.channel.weight > 0);
    return this.selectWeighted(`channel:${channelName}`, eligible, value => value.channel.weight)?.peer;
  }

  selectPlacement(): AdmittedServicePeer | undefined {
    const eligible = this.peers().filter(peer => {
      const descriptor = peer.descriptor;
      return descriptor.state === 'serving'
        && descriptor.objectRole === 'server'
        && descriptor.placementWeight > 0
        && descriptor.activeCapacityUsed < descriptor.activeCapacityLimit
        && descriptor.pendingCapacityUsed < descriptor.pendingCapacityLimit;
    });
    return this.selectWeighted(
      'placement',
      eligible,
      peer => peer.descriptor.placementWeight
    );
  }

  selectObjectPlacement(stableType: string): ServiceNodeDescriptor | undefined {
    requireText(stableType, 'stableType');
    const capability = `object-type:${stableType}`;
    const candidates = [
      this.local,
      ...this.peers().map(peer => peer.descriptor)
    ].filter(descriptor =>
      descriptor.state === 'serving'
      && descriptor.objectRole === 'server'
      && descriptor.placementWeight > 0
      && descriptor.activeCapacityUsed < descriptor.activeCapacityLimit
      && descriptor.pendingCapacityUsed < descriptor.pendingCapacityLimit
      && descriptor.protocolCapabilities.includes(capability)
    );
    return this.selectWeighted(
      `object:${stableType}`,
      candidates,
      descriptor => descriptor.placementWeight
    );
  }

  private selectWeighted<T>(
    key: string,
    eligible: readonly T[],
    weight: (value: T) => number
  ): T | undefined {
    const total = eligible.reduce((sum, value) => sum + BigInt(weight(value)), 0n);
    if (total === 0n) return undefined;
    const cursor = this.selectionCursor.get(key) ?? 0n;
    this.selectionCursor.set(key, cursor + 1n);
    const selected = cursor % total;
    let offset = 0n;
    for (const value of eligible) {
      offset += BigInt(weight(value));
      if (selected < offset) return value;
    }
    return eligible.at(-1);
  }
}

export function validateDescriptor(descriptor: ServiceNodeDescriptor): void {
  requireText(descriptor.meshName, 'meshName');
  requireText(descriptor.nodeRoutingId, 'nodeRoutingId');
  requireText(descriptor.advertisedEndpoint, 'advertisedEndpoint');
  requireText(descriptor.securityIdentity, 'securityIdentity');
  if (descriptor.lifecycleGeneration <= 0n || descriptor.descriptorRevision <= 0n) {
    throw new RangeError('Lifecycle generation and descriptor revision must be non-zero.');
  }
  if (
    !Number.isSafeInteger(descriptor.effectiveMaxMessageBytes)
    || descriptor.effectiveMaxMessageBytes <= 0
    || descriptor.applicationVersion < 0n
  ) {
    throw new RangeError('Descriptor message bound or application version is invalid.');
  }
  validatePublicWeight(descriptor.placementWeight, 'placementWeight');
  validateCapacity(descriptor.activeCapacityLimit, false, 'activeCapacityLimit');
  validateCapacity(descriptor.pendingCapacityLimit, true, 'pendingCapacityLimit');
  validateCapacity(descriptor.activeCapacityUsed, true, 'activeCapacityUsed');
  validateCapacity(descriptor.pendingCapacityUsed, true, 'pendingCapacityUsed');
  if (
    descriptor.activeCapacityUsed > descriptor.activeCapacityLimit
    || descriptor.pendingCapacityUsed > descriptor.pendingCapacityLimit
  ) {
    throw new RangeError('Descriptor capacity use exceeds its limit.');
  }
  validateSortedUnique(
    descriptor.channels,
    channel => {
      requireText(channel.name, 'channel.name');
      validatePublicWeight(channel.weight, 'channel.weight');
      return channel.name;
    },
    'channels'
  );
  validateSortedUnique(
    descriptor.protocolCapabilities,
    capability => {
      requireText(capability, 'protocol capability');
      return capability;
    },
    'protocolCapabilities'
  );
  if (!descriptor.protocolCapabilities.includes(REQUIRED_CAPABILITY)) {
    throw new TypeError(`Descriptor requires capability '${REQUIRED_CAPABILITY}'.`);
  }
}

function validateCapacity(value: number, allowZero: boolean, field: string): void {
  if (!Number.isInteger(value) || value < (allowZero ? 0 : 1) || value > MAX_CAPACITY) {
    throw new RangeError(`${field} is outside its supported range.`);
  }
}

function validatePublicWeight(value: number, field: string): void {
  if (!Number.isInteger(value) || value < 0 || value > 10_000) {
    throw new RangeError(`${field} must be an integer in 0..10000.`);
  }
}

function validateSortedUnique<T>(
  values: readonly T[],
  key: (value: T) => string,
  field: string
): void {
  let previous: string | undefined;
  for (const value of values) {
    const current = key(value);
    if (previous !== undefined && previous >= current) {
      throw new TypeError(`${field} must be sorted and unique.`);
    }
    previous = current;
  }
}

function findChannel(
  descriptor: ServiceNodeDescriptor,
  channelName: string
): ServiceChannelDescriptor | undefined {
  return descriptor.channels.find(channel => channel.name === channelName);
}

function requireText(value: string, field: string): void {
  if (value.length === 0 || value.includes('\0')) {
    throw new TypeError(`${field} must be non-empty text without NUL.`);
  }
}

function cloneDescriptor(descriptor: ServiceNodeDescriptor): ServiceNodeDescriptor {
  return {
    ...descriptor,
    channels: descriptor.channels.map(channel => ({ ...channel })),
    protocolCapabilities: [...descriptor.protocolCapabilities]
  };
}

function clonePeer(peer: AdmittedServicePeer): AdmittedServicePeer {
  return { descriptor: cloneDescriptor(peer.descriptor), connectionId: peer.connectionId };
}

function sameDescriptor(left: ServiceNodeDescriptor, right: ServiceNodeDescriptor): boolean {
  return JSON.stringify(toComparable(left)) === JSON.stringify(toComparable(right));
}

function toComparable(descriptor: ServiceNodeDescriptor): unknown {
  return {
    ...descriptor,
    lifecycleGeneration: descriptor.lifecycleGeneration.toString(),
    descriptorRevision: descriptor.descriptorRevision.toString(),
    applicationVersion: descriptor.applicationVersion.toString()
  };
}
