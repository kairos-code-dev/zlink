export interface ClientServerDescriptor {
  readonly channelName: string;
  readonly serverRoutingId: string;
  readonly lifecycleGeneration: bigint;
  readonly descriptorRevision: bigint;
  readonly weight: number;
  readonly state: 'preparing' | 'serving' | 'retiring' | 'stopped' | 'error';
  readonly securityIdentity: string;
  readonly effectiveMaxMessageBytes: number;
  readonly advertisedEndpoint: string;
}

export interface FanoutPublisherDescriptor {
  readonly channelName: string;
  readonly publisherRoutingId: string;
  readonly lifecycleGeneration: bigint;
  readonly descriptorRevision: bigint;
  readonly advertisedEndpoint: string;
  readonly state: 'preparing' | 'serving' | 'retiring' | 'stopped' | 'error';
}

interface Current<T> {
  readonly descriptor: T;
  readonly connectionId: string;
}

export interface SelectedClientServer {
  readonly descriptor: ClientServerDescriptor;
  readonly connectionId: string;
}

/** Dedicated discovery state; fanout publishers never reuse RouteMesh peer rows. */
export class ServiceDiscoveryRegistry {
  private readonly clientServers = new Map<string, Current<ClientServerDescriptor>>();
  private readonly fanoutPublishers = new Map<string, Current<FanoutPublisherDescriptor>>();
  private readonly clientServerSelectionWeights = new Map<string, Map<string, bigint>>();

  admitClientServer(descriptor: ClientServerDescriptor, connectionId: string): boolean {
    validateClientServer(descriptor);
    return this.admit(
      this.clientServers,
      `${descriptor.channelName}\0${descriptor.serverRoutingId}`,
      descriptor,
      connectionId
    );
  }

  removeClientServer(
    channelName: string,
    serverRoutingId: string,
    connectionId: string
  ): boolean {
    return removeCurrent(this.clientServers, `${channelName}\0${serverRoutingId}`, connectionId);
  }

  selectClientServer(channelName: string): ClientServerDescriptor | undefined {
    return this.selectClientServerConnection(channelName)?.descriptor;
  }

  selectClientServerConnection(channelName: string): SelectedClientServer | undefined {
    const eligible = [...this.clientServers.values()]
      .filter(value =>
        value.descriptor.channelName === channelName
        && value.descriptor.state === 'serving'
        && value.descriptor.weight > 0)
      .sort((left, right) =>
        left.descriptor.serverRoutingId.localeCompare(right.descriptor.serverRoutingId));
    const total = eligible.reduce((sum, value) => sum + BigInt(value.descriptor.weight), 0n);
    if (total === 0n) return undefined;
    const currentWeights = this.clientServerSelectionWeights.get(channelName) ?? new Map();
    this.clientServerSelectionWeights.set(channelName, currentWeights);
    const eligibleIds = new Set(eligible.map(value => value.descriptor.serverRoutingId));
    for (const serverRoutingId of currentWeights.keys()) {
      if (!eligibleIds.has(serverRoutingId)) currentWeights.delete(serverRoutingId);
    }

    let selected = eligible[0]!;
    let selectedWeight: bigint | undefined;
    for (const current of eligible) {
      const serverRoutingId = current.descriptor.serverRoutingId;
      const nextWeight = (currentWeights.get(serverRoutingId) ?? 0n)
        + BigInt(current.descriptor.weight);
      currentWeights.set(serverRoutingId, nextWeight);
      if (selectedWeight === undefined || nextWeight > selectedWeight) {
        selected = current;
        selectedWeight = nextWeight;
      }
    }
    currentWeights.set(
      selected.descriptor.serverRoutingId,
      currentWeights.get(selected.descriptor.serverRoutingId)! - total
    );
    return {
      descriptor: { ...selected.descriptor },
      connectionId: selected.connectionId
    };
  }

  clientServerDescriptors(channelName: string): readonly ClientServerDescriptor[] {
    return [...this.clientServers.values()]
      .map(value => value.descriptor)
      .filter(value => value.channelName === channelName)
      .sort((left, right) => left.serverRoutingId.localeCompare(right.serverRoutingId))
      .map(value => ({ ...value }));
  }

  admitFanoutPublisher(descriptor: FanoutPublisherDescriptor, connectionId: string): boolean {
    validateFanout(descriptor);
    return this.admit(
      this.fanoutPublishers,
      `${descriptor.channelName}\0${descriptor.publisherRoutingId}`,
      descriptor,
      connectionId
    );
  }

  removeFanoutPublisher(
    channelName: string,
    publisherRoutingId: string,
    connectionId: string
  ): boolean {
    return removeCurrent(
      this.fanoutPublishers,
      `${channelName}\0${publisherRoutingId}`,
      connectionId
    );
  }

  fanoutEndpoints(channelName: string): readonly FanoutPublisherDescriptor[] {
    return [...this.fanoutPublishers.values()]
      .map(value => value.descriptor)
      .filter(value => value.channelName === channelName && value.state === 'serving')
      .sort((left, right) => left.publisherRoutingId.localeCompare(right.publisherRoutingId))
      .map(value => ({ ...value }));
  }

  private admit<T extends { readonly lifecycleGeneration: bigint; readonly descriptorRevision: bigint }>(
    rows: Map<string, Current<T>>,
    key: string,
    descriptor: T,
    connectionId: string
  ): boolean {
    requireText(connectionId, 'connectionId');
    const current = rows.get(key);
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
      return false;
    }
    rows.set(key, { descriptor: { ...descriptor }, connectionId });
    return true;
  }
}

function sameDescriptor<T extends { readonly lifecycleGeneration: bigint; readonly descriptorRevision: bigint }>(
  left: T,
  right: T
): boolean {
  return JSON.stringify({
    ...left,
    lifecycleGeneration: left.lifecycleGeneration.toString(),
    descriptorRevision: left.descriptorRevision.toString()
  }) === JSON.stringify({
    ...right,
    lifecycleGeneration: right.lifecycleGeneration.toString(),
    descriptorRevision: right.descriptorRevision.toString()
  });
}

function removeCurrent<T>(
  rows: Map<string, Current<T>>,
  key: string,
  connectionId: string
): boolean {
  const current = rows.get(key);
  if (current === undefined || current.connectionId !== connectionId) return false;
  rows.delete(key);
  return true;
}

function validateClientServer(descriptor: ClientServerDescriptor): void {
  requireText(descriptor.channelName, 'channelName');
  requireText(descriptor.serverRoutingId, 'serverRoutingId');
  requireText(descriptor.securityIdentity, 'securityIdentity');
  requireText(descriptor.advertisedEndpoint, 'advertisedEndpoint');
  validateRevision(descriptor.lifecycleGeneration, descriptor.descriptorRevision);
  if (!Number.isInteger(descriptor.weight) || descriptor.weight < 0 || descriptor.weight > 10_000) {
    throw new RangeError('ClientServer weight must be an integer in 0..10000.');
  }
  if (!Number.isSafeInteger(descriptor.effectiveMaxMessageBytes) || descriptor.effectiveMaxMessageBytes < 1) {
    throw new RangeError('ClientServer message bound must be positive.');
  }
}

function validateFanout(descriptor: FanoutPublisherDescriptor): void {
  requireText(descriptor.channelName, 'channelName');
  requireText(descriptor.publisherRoutingId, 'publisherRoutingId');
  requireText(descriptor.advertisedEndpoint, 'advertisedEndpoint');
  validateRevision(descriptor.lifecycleGeneration, descriptor.descriptorRevision);
}

function validateRevision(lifecycle: bigint, revision: bigint): void {
  if (lifecycle <= 0n || revision <= 0n) {
    throw new RangeError('Lifecycle generation and descriptor revision must be non-zero.');
  }
}

function requireText(value: string, field: string): void {
  if (value.length === 0 || value.includes('\0')) {
    throw new TypeError(`${field} must be non-empty text without NUL.`);
  }
}
