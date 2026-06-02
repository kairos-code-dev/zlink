import { ZLinkNodeBackendAdapterFactory } from '../backend';
import type {
  ZLinkBackendAdapterFactory,
  ZLinkBackendContext,
  ZLinkBackendRegistry,
  ZLinkBackendRegistryQueryClient
} from '../backend';
import type {
  RoutingId,
  ZLinkMemberPeerEntry,
  ZLinkRegistryOptions,
  ZLinkRegistryQuery,
  ZLinkRegistryQueryClient,
  ZLinkRegistryQueryClientOptions,
  ZLinkRegistryServiceSummaryEntry,
  ZLinkRegistryServiceSummaryFilter,
  ZLinkRegistryStatus,
  ZLinkRegistryTopologyEntry,
  ZLinkRegistryTopologyFilter,
  ZLinkSpotRemoteAddress,
  ZLinkSpotRemoteAddressResolver
} from '../../contracts';
import {
  ZLinkAutoConnectType,
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkSpotKind
} from '../../contracts';
import type { ZLinkBackendDiscovery } from '../backend';
import { ZLinkConfigurationException, type ZLinkFrameworkRegistration } from '../configuration';

export interface ZLinkRegistryRuntimeOptions {
  readonly registration: ZLinkRegistryOptions;
}

export class ZLinkRegistryRuntime {
  private readonly backendAdapterFactory: ZLinkBackendAdapterFactory;
  private readonly registration: NormalizedRegistryOptions;
  private readonly gate = new AsyncGate();
  private context?: ZLinkBackendContext;
  private registry?: ZLinkBackendRegistry;

  constructor(options: ZLinkRegistryRuntimeOptions, internalOptions?: unknown) {
    this.backendAdapterFactory = resolveBackendAdapterFactory(internalOptions);
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
}

export class DefaultZLinkRegistryQueryClient implements ZLinkRegistryQueryClient {
  private readonly context: ZLinkBackendContext;
  private readonly client: ZLinkBackendRegistryQueryClient;
  private disposed = false;

  constructor(options: ZLinkRegistryQueryClientServiceOptions, internalOptions?: unknown) {
    const registration = normalizeRegistryQueryClientOptions(options.registration);
    const backendAdapterFactory = resolveBackendAdapterFactory(internalOptions);
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

export interface ZLinkRegistrySpotRemoteAddressResolverOptions {
  readonly registration: ZLinkFrameworkRegistration;
}

export class ZLinkRegistrySpotRemoteAddressResolver implements ZLinkSpotRemoteAddressResolver {
  private readonly backendAdapterFactory: ZLinkBackendAdapterFactory;
  private readonly registration: ZLinkFrameworkRegistration;
  private readonly routerChannelId: string;
  private readonly context: ZLinkBackendContext;
  private readonly discovery: ZLinkBackendDiscovery;
  private disposed = false;

  constructor(options: ZLinkRegistrySpotRemoteAddressResolverOptions, internalOptions?: unknown) {
    this.backendAdapterFactory = resolveBackendAdapterFactory(internalOptions);
    this.registration = options.registration;
    const registryOptions = this.registration.registrySpotRemoteAddresses
      ?? failConfiguration('Registry SPOT routes are not configured.');
    this.routerChannelId = resolveRouterChannelId(this.registration, registryOptions.routerChannelId);

    const channelAdapter = this.backendAdapterFactory.createChannelAdapter();
    this.context = channelAdapter.createContext();
    this.discovery = channelAdapter.createDiscovery(
      this.context,
      ZLinkAutoConnectType.ClientServer,
      registryOptions.namespace
    );
    this.discovery.spotOwnerSyncEnabled = true;
    this.discovery.connectRegistry(registryOptions.registryEndpoint);
  }

  async resolve(spotRid: RoutingId, signal?: AbortSignal): Promise<ZLinkSpotRemoteAddress> {
    throwIfAborted(signal);
    if (this.disposed) {
      throw new ZLinkConfigurationException('Registry SPOT remote address resolver is disposed.');
    }

    try {
      const route = this.discovery.resolveSpot(spotRid);
      return {
        routerChannelId: this.routerChannelId,
        targetNodeRid: String(route.ownerNodeRid),
        spotRid: String(route.spotRid),
        spotKind: route.spotKind as ZLinkSpotKind
      };
    } catch (error) {
      if (isNotFoundError(error)) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.SpotRouteNotFound,
          `SPOT route was not found for '${spotRid}'.`,
          true,
          error
        );
      }
      throw error;
    }
  }

  async dispose(): Promise<void> {
    if (this.disposed) {
      return;
    }
    this.disposed = true;
    await this.discovery.dispose();
    await this.context.dispose();
  }
}

function resolveBackendAdapterFactory(internalOptions: unknown): ZLinkBackendAdapterFactory {
  if (
    typeof internalOptions === 'object'
    && internalOptions !== null
    && 'backendAdapterFactory' in internalOptions
  ) {
    const factory = (internalOptions as { readonly backendAdapterFactory?: ZLinkBackendAdapterFactory }).backendAdapterFactory;
    if (factory !== undefined) {
      return factory;
    }
  }
  return new ZLinkNodeBackendAdapterFactory();
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

function resolveRouterChannelId(
  registration: ZLinkFrameworkRegistration,
  configuredRouterChannelId: string | undefined
): string {
  if (configuredRouterChannelId !== undefined) {
    return configuredRouterChannelId;
  }
  const [routerChannelId] = registration.routeChannels;
  if (routerChannelId === undefined || registration.routeChannels.size !== 1) {
    throw new ZLinkConfigurationException(
      'Registry SPOT remote address resolver requires RouterChannelId when more than one route mesh channel is registered.'
    );
  }
  return routerChannelId;
}

function failConfiguration(message: string): never {
  throw new ZLinkConfigurationException(message);
}

function isNotFoundError(error: unknown): boolean {
  if (typeof error !== 'object' || error === null) {
    return false;
  }
  const errno = (error as { readonly internalErrno?: unknown; readonly errno?: unknown }).internalErrno
    ?? (error as { readonly errno?: unknown }).errno;
  return errno === 2;
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
