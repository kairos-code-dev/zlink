import type {
  ZLinkClientServerStatus,
  ZLinkClientServerRuntime,
  ZLinkClientServerTargetStatus,
  ZLinkFanoutStatus,
  ZLinkFanoutRuntime,
  ZLinkPeerStatus
} from '../../contracts';
import {
  ZLinkFrameworkRuntimeState,
  ZLinkPeerState,
  ZLinkTopologyReason,
  ZLinkTopologyState
} from '../../contracts';
import { ZLinkConfigurationException } from '../configuration';
import type { ZLinkChannelRuntimeManager } from '../channels/channel-runtime-manager';

type RuntimeAccessor = () => ZLinkChannelRuntimeManager | undefined;
type HostStateAccessor = () => ZLinkFrameworkRuntimeState;
interface HostObserver {
  readonly changed: () => void;
  readonly stop: () => void;
}

export class ZLinkClientServerRuntimeProjection implements ZLinkClientServerRuntime {
  private sequence = 0n;
  private readonly hostObservers = new Set<HostObserver>();

  constructor(
    private readonly runtime: RuntimeAccessor,
    private readonly hostState: HostStateAccessor =
      () => ZLinkFrameworkRuntimeState.Serving
  ) {}

  snapshot(channelName: string): ZLinkClientServerStatus {
    const topology = this.requireRuntime().clientServerTopology(channelName);
    if (topology.localRole === undefined) {
      throw new ZLinkConfigurationException(`ClientServer channel '${channelName}' is not registered.`);
    }
    const targets = topology.descriptors.map((descriptor): ZLinkClientServerTargetStatus => ({
      nodeRid: descriptor.serverRoutingId,
      weight: descriptor.weight,
      state: descriptor.state === 'serving' ? ZLinkPeerState.Ready
        : descriptor.state === 'retiring' ? ZLinkPeerState.Draining
          : descriptor.state === 'preparing' ? ZLinkPeerState.Connecting
            : ZLinkPeerState.NotConnected,
      unavailableReason: descriptor.state === 'serving' && descriptor.weight > 0
        ? undefined
        : descriptor.state === 'retiring'
          ? ZLinkTopologyReason.Draining
          : ZLinkTopologyReason.NoReadyTarget
    }));
    const readyTargetCount = targets.filter(
      target => target.state === ZLinkPeerState.Ready && target.weight > 0
    ).length;
    const hostState = this.hostState();
    const hostReady = hostState === ZLinkFrameworkRuntimeState.Serving;
    return {
      channelName,
      localRole: topology.localRole,
      state: hostReady
        ? readyTargetCount > 0
          ? ZLinkTopologyState.Ready
          : ZLinkTopologyState.Degraded
        : topologyStateForHost(hostState),
      isReady: hostReady && readyTargetCount > 0,
      readyTargetCount,
      targets,
      sequence: this.sequence,
      observedAt: new Date()
    };
  }

  observe(channelName: string, capacity = 64, signal?: AbortSignal): AsyncIterable<ZLinkClientServerStatus> {
    const runtime = this.requireRuntime();
    const queue = new RuntimeEventQueue<ZLinkClientServerStatus>(capacity, signal);
    let lastSnapshot = this.snapshot(channelName);
    const stop = runtime.observeClientServerTopology(channelName, () => {
      this.sequence += 1n;
      lastSnapshot = this.snapshot(channelName);
      queue.push(lastSnapshot);
    });
    const hostObserver: HostObserver = {
      changed: () => {
        this.sequence += 1n;
        lastSnapshot = this.snapshot(channelName);
        queue.push(lastSnapshot);
      },
      stop: () => {
        try {
          this.sequence += 1n;
          let current = lastSnapshot;
          try {
            current = this.snapshot(channelName);
            lastSnapshot = current;
          } catch {
            // The last complete projection remains valid after native teardown.
          }
          queue.seal({
            ...current,
            state: ZLinkTopologyState.Stopped,
            isReady: false,
            sequence: this.sequence,
            observedAt: new Date()
          });
        } finally {
          void queue.return();
        }
      }
    };
    this.hostObservers.add(hostObserver);
    queue.onClose(() => {
      stop();
      this.hostObservers.delete(hostObserver);
    });
    return queue;
  }

  hostStateChanged(): void {
    for (const observer of this.hostObservers) {
      try {
        observer.changed();
      } catch {
        // Monitoring projection failures do not change host lifecycle results.
      }
    }
  }

  stopObservers(): void {
    for (const observer of [...this.hostObservers]) {
      try {
        observer.stop();
      } catch {
        // The observer is still removed when its terminal snapshot fails.
      }
    }
    this.hostObservers.clear();
  }

  isReady(channelName: string): boolean {
    return this.snapshot(channelName).isReady;
  }

  private requireRuntime(): ZLinkChannelRuntimeManager {
    const runtime = this.runtime();
    if (runtime === undefined) throw new ZLinkConfigurationException('ClientServer runtime has not started.');
    return runtime;
  }
}

export class ZLinkFanoutRuntimeProjection implements ZLinkFanoutRuntime {
  private sequence = 0n;
  private readonly hostObservers = new Set<HostObserver>();

  constructor(
    private readonly runtime: RuntimeAccessor,
    private readonly hostState: HostStateAccessor =
      () => ZLinkFrameworkRuntimeState.Serving
  ) {}

  snapshot(channelName: string): ZLinkFanoutStatus {
    const publishers = this.requireRuntime().fanoutTopology(channelName).descriptors
      .map((descriptor): ZLinkPeerStatus => ({
        nodeRid: descriptor.publisherRoutingId,
        state: descriptor.state === 'serving' ? ZLinkPeerState.Ready
          : descriptor.state === 'retiring' ? ZLinkPeerState.Draining
            : descriptor.state === 'preparing' ? ZLinkPeerState.Connecting
              : ZLinkPeerState.NotConnected,
        unavailableReason: descriptor.state === 'serving'
          ? undefined
          : descriptor.state === 'retiring'
            ? ZLinkTopologyReason.Draining
            : ZLinkTopologyReason.NoReadyTarget
      }));
    const readyPublisherCount = publishers.filter(
      publisher => publisher.state === ZLinkPeerState.Ready
    ).length;
    const hostState = this.hostState();
    const hostReady = hostState === ZLinkFrameworkRuntimeState.Serving;
    return {
      channelName,
      state: hostReady
        ? readyPublisherCount > 0
          ? ZLinkTopologyState.Ready
          : ZLinkTopologyState.Degraded
        : topologyStateForHost(hostState),
      isReady: hostReady && readyPublisherCount > 0,
      readyPublisherCount,
      publishers,
      sequence: this.sequence,
      observedAt: new Date()
    };
  }

  observe(channelName: string, capacity = 64, signal?: AbortSignal): AsyncIterable<ZLinkFanoutStatus> {
    const runtime = this.requireRuntime();
    const queue = new RuntimeEventQueue<ZLinkFanoutStatus>(capacity, signal);
    let lastSnapshot = this.snapshot(channelName);
    const stop = runtime.observeFanoutTopology(channelName, () => {
      this.sequence += 1n;
      lastSnapshot = this.snapshot(channelName);
      queue.push(lastSnapshot);
    });
    const hostObserver: HostObserver = {
      changed: () => {
        this.sequence += 1n;
        lastSnapshot = this.snapshot(channelName);
        queue.push(lastSnapshot);
      },
      stop: () => {
        try {
          this.sequence += 1n;
          let current = lastSnapshot;
          try {
            current = this.snapshot(channelName);
            lastSnapshot = current;
          } catch {
            // The last complete projection remains valid after native teardown.
          }
          queue.seal({
            ...current,
            state: ZLinkTopologyState.Stopped,
            isReady: false,
            sequence: this.sequence,
            observedAt: new Date()
          });
        } finally {
          void queue.return();
        }
      }
    };
    this.hostObservers.add(hostObserver);
    queue.onClose(() => {
      stop();
      this.hostObservers.delete(hostObserver);
    });
    return queue;
  }

  hostStateChanged(): void {
    for (const observer of this.hostObservers) {
      try {
        observer.changed();
      } catch {
        // Monitoring projection failures do not change host lifecycle results.
      }
    }
  }

  stopObservers(): void {
    for (const observer of [...this.hostObservers]) {
      try {
        observer.stop();
      } catch {
        // The observer is still removed when its terminal snapshot fails.
      }
    }
    this.hostObservers.clear();
  }

  private requireRuntime(): ZLinkChannelRuntimeManager {
    const runtime = this.runtime();
    if (runtime === undefined) throw new ZLinkConfigurationException('Fanout runtime has not started.');
    return runtime;
  }
}

function topologyStateForHost(state: ZLinkFrameworkRuntimeState): ZLinkTopologyState {
  switch (state) {
    case ZLinkFrameworkRuntimeState.Preparing:
      return ZLinkTopologyState.Starting;
    case ZLinkFrameworkRuntimeState.Relocating:
    case ZLinkFrameworkRuntimeState.Relocated:
    case ZLinkFrameworkRuntimeState.Draining:
      return ZLinkTopologyState.Stopping;
    case ZLinkFrameworkRuntimeState.Stopped:
      return ZLinkTopologyState.Stopped;
    case ZLinkFrameworkRuntimeState.Error:
      return ZLinkTopologyState.Failed;
    case ZLinkFrameworkRuntimeState.Serving:
      return ZLinkTopologyState.Degraded;
  }
}

export class RuntimeEventQueue<T> implements AsyncIterable<T>, AsyncIterator<T> {
  private readonly values: Array<T | undefined> = [];
  private valuesHead = 0;
  private valuesCount = 0;
  private readonly waiters: Array<((result: IteratorResult<T>) => void) | undefined> = [];
  private waitersHead = 0;
  private waitersCount = 0;
  private cleanup?: () => void;
  private closed = false;

  constructor(private readonly capacity: number, signal?: AbortSignal) {
    if (!Number.isInteger(capacity) || capacity <= 0) throw new RangeError('Observer capacity must be positive.');
    signal?.addEventListener('abort', () => this.close(), { once: true });
  }

  [Symbol.asyncIterator](): AsyncIterator<T> { return this; }

  next(): Promise<IteratorResult<T>> {
    const value = this.takeValue();
    if (value !== undefined) return Promise.resolve({ done: false, value });
    if (this.closed) return Promise.resolve({ done: true, value: undefined });
    return new Promise(resolve => {
      this.waiters.push(resolve);
      this.waitersCount += 1;
    });
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
    const waiter = this.takeWaiter();
    if (waiter !== undefined) waiter({ done: false, value });
    else {
      if (this.valuesCount === this.capacity) this.takeValue();
      this.values.push(value);
      this.valuesCount += 1;
    }
  }

  seal(value: T): void {
    if (this.closed) return;
    this.clearValues();
    const waiter = this.takeWaiter();
    if (waiter !== undefined) waiter({ done: false, value });
    else {
      this.values.push(value);
      this.valuesCount = 1;
    }
    this.close();
  }

  private close(): void {
    if (this.closed) return;
    this.closed = true;
    this.cleanup?.();
    this.cleanup = undefined;
    let waiter: ((result: IteratorResult<T>) => void) | undefined;
    while ((waiter = this.takeWaiter()) !== undefined) {
      waiter({ done: true, value: undefined });
    }
  }

  private takeValue(): T | undefined {
    if (this.valuesCount === 0) return undefined;
    const value = this.values[this.valuesHead];
    this.values[this.valuesHead] = undefined;
    this.valuesHead += 1;
    this.valuesCount -= 1;
    if (this.valuesCount === 0) {
      this.clearValues();
    } else if (this.valuesHead >= 1024 && this.valuesHead * 2 >= this.values.length) {
      this.values.splice(0, this.valuesHead);
      this.valuesHead = 0;
    }
    return value;
  }

  private takeWaiter(): ((result: IteratorResult<T>) => void) | undefined {
    if (this.waitersCount === 0) return undefined;
    const waiter = this.waiters[this.waitersHead];
    this.waiters[this.waitersHead] = undefined;
    this.waitersHead += 1;
    this.waitersCount -= 1;
    if (this.waitersCount === 0) {
      this.waiters.length = 0;
      this.waitersHead = 0;
    } else if (this.waitersHead >= 1024 && this.waitersHead * 2 >= this.waiters.length) {
      this.waiters.splice(0, this.waitersHead);
      this.waitersHead = 0;
    }
    return waiter;
  }

  private clearValues(): void {
    this.values.length = 0;
    this.valuesHead = 0;
    this.valuesCount = 0;
  }
}
