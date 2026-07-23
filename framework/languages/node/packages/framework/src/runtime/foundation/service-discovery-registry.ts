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

/** Dedicated discovery state; fanout publishers never reuse RouteMesh peer rows. */
export class ServiceDiscoveryRegistry {
  private readonly clientServers = new Map<string, Current<ClientServerDescriptor>>();
  private readonly fanoutPublishers = new Map<string, Current<FanoutPublisherDescriptor>>();
  private readonly cursors = new Map<string, bigint>();

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
    const eligible = [...this.clientServers.values()]
      .map(value => value.descriptor)
      .filter(value => value.channelName === channelName && value.state === 'serving' && value.weight > 0)
      .sort((left, right) => left.serverRoutingId.localeCompare(right.serverRoutingId));
    const total = eligible.reduce((sum, value) => sum + BigInt(value.weight), 0n);
    if (total === 0n) return undefined;
    const cursor = this.cursors.get(channelName) ?? 0n;
    this.cursors.set(channelName, cursor + 1n);
    const selected = cursor % total;
    let offset = 0n;
    for (const descriptor of eligible) {
      offset += BigInt(descriptor.weight);
      if (selected < offset) return { ...descriptor };
    }
    return eligible.at(-1);
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
  if (!Number.isInteger(descriptor.weight) || descriptor.weight < 0 || descriptor.weight > 100) {
    throw new RangeError('ClientServer weight must be in the range 0..100.');
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
