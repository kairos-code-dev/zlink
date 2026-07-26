import type {
  RoutingId,
  ZLinkClientServerChannelSnapshot,
  ZLinkClientServerRuntime,
  ZLinkClientServerRuntimeEvent,
  ZLinkClientServerServerSnapshot,
  ZLinkFanoutChannelSnapshot,
  ZLinkFanoutPublisherConnectionSnapshot,
  ZLinkFanoutRuntime,
  ZLinkFanoutRuntimeEvent
} from '../../contracts';
import { ZLinkConfigurationException } from '../configuration';
import type { ZLinkChannelRuntimeManager } from '../channels/channel-runtime-manager';

type RuntimeAccessor = () => ZLinkChannelRuntimeManager | undefined;

export class ZLinkClientServerRuntimeProjection implements ZLinkClientServerRuntime {
  private sequence = 0n;

  constructor(private readonly runtime: RuntimeAccessor) {}

  snapshot(channelName: string): ZLinkClientServerChannelSnapshot {
    const topology = this.requireRuntime().clientServerTopology(channelName);
    if (topology.localRole === undefined) {
      throw new ZLinkConfigurationException(`ClientServer channel '${channelName}' is not registered.`);
    }
    const servers = topology.descriptors.map((descriptor): ZLinkClientServerServerSnapshot => ({
      serverRid: descriptor.serverRoutingId as RoutingId,
      lifecycleGeneration: descriptor.lifecycleGeneration,
      descriptorRevision: descriptor.descriptorRevision,
      endpoint: descriptor.advertisedEndpoint,
      weight: descriptor.weight,
      ready: descriptor.state === 'serving' && descriptor.weight > 0,
      state: descriptor.state === 'serving' ? 'ready'
        : descriptor.state === 'retiring' ? 'draining'
          : descriptor.state === 'preparing' ? 'connecting'
            : descriptor.state === 'stopped' ? 'disconnected'
              : 'rejected',
      descriptorSource: 'runtime'
    }));
    return {
      channelName,
      localRole: topology.localRole,
      selectable: servers.some(server => server.ready && server.weight > 0),
      readyServerCount: servers.filter(server => server.ready).length,
      connectionIntentCount: servers.length,
      pendingRequestCount: topology.pendingRequestCount,
      sequence: this.sequence,
      observedAt: new Date(),
      servers,
      location: { state: 'ready' }
    };
  }

  observe(channelName: string, capacity = 64, signal?: AbortSignal): AsyncIterable<ZLinkClientServerRuntimeEvent> {
    const runtime = this.requireRuntime();
    const queue = new RuntimeEventQueue<ZLinkClientServerRuntimeEvent>(capacity, signal);
    const stop = runtime.observeClientServerTopology(channelName, () => {
      const snapshot = this.snapshot(channelName);
      this.sequence += 1n;
      const servers = snapshot.servers.length === 0 ? [undefined] : snapshot.servers;
      for (const server of servers) {
        queue.push({
          identifier: 'zlink.runtime.client_server.server_changed',
          sequence: this.sequence,
          timestamp: new Date(),
          channelName,
          serverRid: server?.serverRid,
          lifecycleGeneration: server?.lifecycleGeneration,
          descriptorRevision: server?.descriptorRevision,
          weight: server?.weight,
          ready: server?.ready,
          state: server?.state
        });
      }
    });
    queue.onClose(stop);
    return queue;
  }

  isReady(channelName: string): boolean {
    return this.snapshot(channelName).selectable;
  }

  private requireRuntime(): ZLinkChannelRuntimeManager {
    const runtime = this.runtime();
    if (runtime === undefined) throw new ZLinkConfigurationException('ClientServer runtime has not started.');
    return runtime;
  }
}

export class ZLinkFanoutRuntimeProjection implements ZLinkFanoutRuntime {
  private sequence = 0n;

  constructor(private readonly runtime: RuntimeAccessor) {}

  snapshot(channelName: string): ZLinkFanoutChannelSnapshot {
    const publishers = this.requireRuntime().fanoutTopology(channelName).descriptors
      .map((descriptor): ZLinkFanoutPublisherConnectionSnapshot => ({
        publisherRid: descriptor.publisherRoutingId as RoutingId,
        lifecycleGeneration: descriptor.lifecycleGeneration,
        descriptorRevision: descriptor.descriptorRevision,
        endpoint: descriptor.advertisedEndpoint,
        connectionIntent: true,
        ready: descriptor.state === 'serving',
        state: descriptor.state === 'serving' ? 'ready'
          : descriptor.state === 'retiring' ? 'excluded_draining'
            : descriptor.state === 'stopped' ? 'disconnected'
              : 'connecting'
      }));
    return {
      channelName,
      connectionIntentCount: publishers.length,
      readyConnectionCount: publishers.filter(publisher => publisher.ready).length,
      sequence: this.sequence,
      observedAt: new Date(),
      publishers,
      location: { state: 'ready' }
    };
  }

  observe(channelName: string, capacity = 64, signal?: AbortSignal): AsyncIterable<ZLinkFanoutRuntimeEvent> {
    const runtime = this.requireRuntime();
    const queue = new RuntimeEventQueue<ZLinkFanoutRuntimeEvent>(capacity, signal);
    let previous = new Map<string, ZLinkFanoutPublisherConnectionSnapshot>();
    const stop = runtime.observeFanoutTopology(channelName, () => {
      const snapshot = this.snapshot(channelName);
      this.sequence += 1n;
      const current = new Map(snapshot.publishers.map(entry => [String(entry.publisherRid), entry]));
      for (const [rid, entry] of previous) {
        if (!current.has(rid)) {
          queue.push({
            identifier: 'zlink.runtime.fanout.publisher_changed',
            sequence: this.sequence,
            timestamp: new Date(),
            channelName,
            entry: { ...entry, ready: false, state: 'disconnected' }
          });
        }
      }
      for (const entry of current.values()) {
        queue.push({
          identifier: 'zlink.runtime.fanout.publisher_changed',
          sequence: this.sequence,
          timestamp: new Date(),
          channelName,
          entry
        });
      }
      previous = current;
    });
    queue.onClose(stop);
    return queue;
  }

  private requireRuntime(): ZLinkChannelRuntimeManager {
    const runtime = this.runtime();
    if (runtime === undefined) throw new ZLinkConfigurationException('Fanout runtime has not started.');
    return runtime;
  }
}

export class RuntimeEventQueue<T> implements AsyncIterable<T>, AsyncIterator<T> {
  private readonly values: T[] = [];
  private readonly waiters: Array<(result: IteratorResult<T>) => void> = [];
  private cleanup?: () => void;
  private closed = false;

  constructor(private readonly capacity: number, signal?: AbortSignal) {
    if (!Number.isInteger(capacity) || capacity <= 0) throw new RangeError('Observer capacity must be positive.');
    signal?.addEventListener('abort', () => this.close(), { once: true });
  }

  [Symbol.asyncIterator](): AsyncIterator<T> { return this; }

  next(): Promise<IteratorResult<T>> {
    const value = this.values.shift();
    if (value !== undefined) return Promise.resolve({ done: false, value });
    if (this.closed) return Promise.resolve({ done: true, value: undefined });
    return new Promise(resolve => this.waiters.push(resolve));
  }

  return(): Promise<IteratorResult<T>> {
    this.close();
    return Promise.resolve({ done: true, value: undefined });
  }

  onClose(cleanup: () => void): void {
    if (this.closed) cleanup();
    else this.cleanup = cleanup;
  }

  push(value: T): void {
    if (this.closed) return;
    const waiter = this.waiters.shift();
    if (waiter !== undefined) waiter({ done: false, value });
    else {
      if (this.values.length === this.capacity) this.values.shift();
      this.values.push(value);
    }
  }

  private close(): void {
    if (this.closed) return;
    this.closed = true;
    this.cleanup?.();
    this.cleanup = undefined;
    for (const waiter of this.waiters.splice(0)) waiter({ done: true, value: undefined });
  }
}
