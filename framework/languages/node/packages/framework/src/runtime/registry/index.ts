import { ZLinkNodeBackendAdapterFactory } from '../backend';
import type {
  ZLinkBackendAdapterFactory,
  ZLinkBackendContext,
  ZLinkBackendRegistry,
  ZLinkBackendRegistryQueryClient
} from '../backend';
import type {
  ZLinkMemberPeerEntry,
  ZLinkRegistryOptions,
  ZLinkRegistryQuery,
  ZLinkRegistryQueryClient,
  ZLinkRegistryQueryClientOptions,
  ZLinkRegistryServiceSummaryEntry,
  ZLinkRegistryServiceSummaryFilter,
  ZLinkRegistryStatus,
  ZLinkRegistryTopologyEntry,
  ZLinkRegistryTopologyFilter
} from '../../contracts';
import { ZLinkConfigurationException } from '../configuration';

export interface ZLinkRegistryRuntimeOptions {
  readonly registration: ZLinkRegistryOptions;
  readonly backendAdapterFactory?: ZLinkBackendAdapterFactory;
}

export class ZLinkRegistryRuntime {
  private readonly backendAdapterFactory: ZLinkBackendAdapterFactory;
  private readonly registration: NormalizedRegistryOptions;
  private readonly gate = new AsyncGate();
  private context?: ZLinkBackendContext;
  private registry?: ZLinkBackendRegistry;

  constructor(options: ZLinkRegistryRuntimeOptions) {
    this.backendAdapterFactory = options.backendAdapterFactory ?? new ZLinkNodeBackendAdapterFactory();
    this.registration = normalizeRegistryOptions(options.registration);
  }

  get isStarted(): boolean {
    return this.registry !== undefined;
  }

  async start(signal?: AbortSignal): Promise<void> {
    await this.gate.run(async () => {
      throwIfAborted(signal);
      if (this.registry !== undefined) {
        return;
      }

      const channelAdapter = this.backendAdapterFactory.createChannelAdapter();
      const registryAdapter = this.backendAdapterFactory.createRegistryAdapter();
      const context = channelAdapter.createContext();
      const registry = registryAdapter.createRegistry(context);
      try {
        if (this.registration.registryId !== 0) {
          registry.setId(this.registration.registryId);
        }
        registry.setHeartbeat(
          this.registration.heartbeatIntervalMs,
          this.registration.heartbeatTimeoutMs
        );
        registry.setBroadcastInterval(this.registration.broadcastIntervalMs);
        for (const peer of this.registration.peers) {
          registry.addPeer(peer);
        }
        registry.bind(this.registration.pubEndpoint, this.registration.routerEndpoint);
        this.context = context;
        this.registry = registry;
      } catch (error) {
        await registry.dispose();
        await context.dispose();
        throw error;
      }
    });
  }

  async stop(signal?: AbortSignal): Promise<void> {
    const owned = await this.gate.run(async () => {
      throwIfAborted(signal);
      const registry = this.registry;
      const context = this.context;
      this.registry = undefined;
      this.context = undefined;
      return { registry, context };
    });

    await owned.registry?.dispose();
    await owned.context?.dispose();
  }

  async execute<T>(action: (registry: ZLinkBackendRegistry) => T, signal?: AbortSignal): Promise<T> {
    if (this.registry === undefined) {
      await this.start(signal);
    }
    return await this.gate.run(async () => {
      throwIfAborted(signal);
      const registry = this.registry;
      if (registry === undefined) {
        throw new Error('Embedded registry runtime is not started.');
      }
      return action(registry);
    });
  }

  async onApplicationBootstrap(): Promise<void> {
    await this.start();
  }

  async onApplicationShutdown(): Promise<void> {
    await this.stop();
  }
}

export class DefaultZLinkRegistryQuery implements ZLinkRegistryQuery {
  constructor(private readonly runtime: ZLinkRegistryRuntime) {}

  statusAsync(signal?: AbortSignal): Promise<ZLinkRegistryStatus> {
    return this.runtime.execute((registry) => registry.status() as ZLinkRegistryStatus, signal);
  }

  serviceSummaryAsync(
    filter?: ZLinkRegistryServiceSummaryFilter,
    signal?: AbortSignal
  ): Promise<readonly ZLinkRegistryServiceSummaryEntry[]> {
    return this.runtime.execute(
      (registry) => registry.serviceSummary(filter) as readonly ZLinkRegistryServiceSummaryEntry[],
      signal
    );
  }

  topologyAsync(
    filter?: ZLinkRegistryTopologyFilter,
    signal?: AbortSignal
  ): Promise<readonly ZLinkRegistryTopologyEntry[]> {
    return this.runtime.execute(
      (registry) => registry.topology(filter as never) as unknown as readonly ZLinkRegistryTopologyEntry[],
      signal
    );
  }

  memberPeersAsync(channelName: string, signal?: AbortSignal): Promise<readonly ZLinkMemberPeerEntry[]> {
    return this.runtime.execute(
      (registry) => registry.memberPeers(channelName) as unknown as readonly ZLinkMemberPeerEntry[],
      signal
    );
  }
}

export interface ZLinkRegistryQueryClientServiceOptions {
  readonly registration: ZLinkRegistryQueryClientOptions;
  readonly backendAdapterFactory?: ZLinkBackendAdapterFactory;
}

export class DefaultZLinkRegistryQueryClient implements ZLinkRegistryQueryClient {
  private readonly context: ZLinkBackendContext;
  private readonly client: ZLinkBackendRegistryQueryClient;
  private disposed = false;

  constructor(options: ZLinkRegistryQueryClientServiceOptions) {
    const registration = normalizeRegistryQueryClientOptions(options.registration);
    const backendAdapterFactory = options.backendAdapterFactory ?? new ZLinkNodeBackendAdapterFactory();
    const channelAdapter = backendAdapterFactory.createChannelAdapter();
    const registryAdapter = backendAdapterFactory.createRegistryAdapter();
    this.context = channelAdapter.createContext();
    this.client = registryAdapter.createRegistryQueryClient(this.context);
    this.client.connect(registration.endpoint);
  }

  async topologyAsync(
    filter?: ZLinkRegistryTopologyFilter,
    signal?: AbortSignal
  ): Promise<readonly ZLinkRegistryTopologyEntry[]> {
    throwIfAborted(signal);
    if (this.disposed) {
      throw new Error('Registry query client is disposed.');
    }
    return this.client.topology(filter as never) as unknown as readonly ZLinkRegistryTopologyEntry[];
  }

  async dispose(): Promise<void> {
    if (this.disposed) {
      return;
    }
    this.disposed = true;
    await this.client.dispose();
    await this.context.dispose();
  }
}

interface NormalizedRegistryOptions {
  readonly pubEndpoint: string;
  readonly routerEndpoint: string;
  readonly registryId: number;
  readonly heartbeatIntervalMs: number;
  readonly heartbeatTimeoutMs: number;
  readonly broadcastIntervalMs: number;
  readonly peers: readonly string[];
}

function normalizeRegistryOptions(options: ZLinkRegistryOptions): NormalizedRegistryOptions {
  if (options.pubEndpoint.trim().length === 0) {
    throw new ZLinkConfigurationException('Registry pubEndpoint is required.');
  }
  if (options.routerEndpoint.trim().length === 0) {
    throw new ZLinkConfigurationException('Registry routerEndpoint is required.');
  }

  const heartbeatIntervalMs = options.heartbeatIntervalMs ?? 5000;
  const heartbeatTimeoutMs = options.heartbeatTimeoutMs ?? 15000;
  const broadcastIntervalMs = options.broadcastIntervalMs ?? 30000;
  validatePositive('heartbeatIntervalMs', heartbeatIntervalMs);
  validatePositive('heartbeatTimeoutMs', heartbeatTimeoutMs);
  validatePositive('broadcastIntervalMs', broadcastIntervalMs);

  return {
    pubEndpoint: options.pubEndpoint,
    routerEndpoint: options.routerEndpoint,
    registryId: options.registryId ?? 0,
    heartbeatIntervalMs,
    heartbeatTimeoutMs,
    broadcastIntervalMs,
    peers: [...(options.peers ?? [])]
  };
}

function normalizeRegistryQueryClientOptions(
  options: ZLinkRegistryQueryClientOptions
): ZLinkRegistryQueryClientOptions {
  if (options.endpoint.trim().length === 0) {
    throw new ZLinkConfigurationException('Registry query client endpoint is required.');
  }
  return options;
}

function validatePositive(name: string, value: number): void {
  if (!Number.isFinite(value) || value <= 0) {
    throw new ZLinkConfigurationException(`Registry ${name} must be greater than zero.`);
  }
}

function throwIfAborted(signal?: AbortSignal): void {
  if (signal?.aborted) {
    throw signal.reason ?? new Error('Operation aborted.');
  }
}

class AsyncGate {
  private tail = Promise.resolve();

  async run<T>(operation: () => Promise<T>): Promise<T> {
    const previous = this.tail;
    let release!: () => void;
    this.tail = new Promise<void>((resolve) => {
      release = resolve;
    });
    await previous;
    try {
      return await operation();
    } finally {
      release();
    }
  }
}
