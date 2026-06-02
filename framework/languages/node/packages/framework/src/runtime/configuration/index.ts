import type { Type, ZLinkRouteRequestContext, ZLinkRouteSendContext, ZLinkSpot } from '../../contracts';

export interface ZLinkFrameworkRegistration {
  readonly actorFactories: ReadonlyMap<string, Type>;
  readonly spotFactories: ReadonlySet<Type<ZLinkSpot>>;
  readonly channels: ReadonlyMap<string, ZLinkChannelOptions>;
  readonly channelClients: ReadonlySet<string>;
  readonly fanoutPublishers: ReadonlySet<string>;
  readonly routeChannels: ReadonlySet<string>;
  readonly routeChannelOptions: ReadonlyMap<string, ZLinkRouteChannelOptions>;
  readonly spotNodes: ReadonlySet<string>;
  readonly spotPublisherClients: ReadonlySet<string>;
  readonly hasSpotRemoteAddressResolver: boolean;
  readonly hasRegistrySpotRemoteAddresses: boolean;
  readonly spotRemoteAddressResolverType?: Type;
  readonly registrySpotRemoteAddresses?: ZLinkRegistrySpotRemoteAddressesRegistration;
}

export interface ZLinkRegistrySpotRemoteAddressesRegistration {
  readonly namespace: string;
  readonly routerChannelId?: string;
  readonly registryEndpoint: string;
}

export interface ZLinkFrameworkRegistrationOptions {
  readonly actorFactories?: Readonly<Record<string, Type> | Map<string, Type>>;
  readonly spotFactories?: readonly Type<ZLinkSpot>[];
  readonly channels?: Readonly<Record<string, ZLinkChannelOptions>>;
  readonly discovery?: ZLinkDiscoveryOptions;
  readonly routeChannels?: readonly (string | ZLinkRouteChannelOptions)[];
  readonly spotNodes?: readonly string[];
  readonly spotPublisherClients?: readonly string[];
  readonly spotRemoteAddressResolver?: Type;
  readonly registrySpotRemoteAddresses?: {
    readonly namespace: string;
    readonly routerChannelId?: string;
  };
}

export interface ZLinkDiscoveryOptions {
  readonly registries?: readonly string[];
}

export interface ZLinkChannelOptions {
  readonly client?: ZLinkClientCapabilityOptions;
  readonly dealerMesh?: ZLinkDealerMeshChannelOptions;
  readonly publisher?: ZLinkPublisherCapabilityOptions;
  readonly routeMesh?: ZLinkRouteMeshChannelOptions;
  readonly server?: { readonly bind?: string };
  readonly subscriber?: ZLinkClientCapabilityOptions;
}

export interface ZLinkClientCapabilityOptions {
  readonly manualConnections?: readonly string[];
}

export interface ZLinkDealerMeshChannelOptions {
  readonly bind?: string;
  readonly client?: ZLinkClientCapabilityOptions;
}

export interface ZLinkPublisherCapabilityOptions {
  readonly bind?: string;
}

export interface ZLinkRouteMeshChannelOptions {
  readonly bind?: string;
  readonly manualConnections?: readonly string[];
  readonly routingId?: string;
  readonly sendHandlers?: readonly ZLinkRouteChannelSendHandlerRegistration[];
  readonly requestHandlers?: readonly ZLinkRouteChannelRequestHandlerRegistration[];
  readonly handlers?: readonly ZLinkRouteChannelHandlerOptions[];
}

export interface ZLinkRouteChannelOptions {
  readonly routerChannelId: string;
  readonly bind?: string;
  readonly manualConnections?: readonly string[];
  readonly routingId?: string;
  readonly sendHandlers?: readonly ZLinkRouteChannelSendHandlerRegistration[];
  readonly requestHandlers?: readonly ZLinkRouteChannelRequestHandlerRegistration[];
  readonly handlers?: readonly ZLinkRouteChannelHandlerOptions[];
}

export interface ZLinkRouteChannelHandlerOptions {
  readonly kind: 'send' | 'request';
  readonly packetName: string;
  readonly handler: ZLinkRouteChannelSendHandler | ZLinkRouteChannelRequestHandler;
}

export interface ZLinkRouteChannelSendHandlerRegistration {
  readonly packetName: string;
  readonly handler: ZLinkRouteChannelSendHandler;
}

export interface ZLinkRouteChannelRequestHandlerRegistration {
  readonly packetName: string;
  readonly handler: ZLinkRouteChannelRequestHandler;
}

export interface ZLinkRouteChannelSendHandler {
  handle(payload: Buffer, context: ZLinkRouteSendContext): Promise<void> | void;
}

export interface ZLinkRouteChannelRequestHandler {
  handle(payload: Buffer, context: ZLinkRouteRequestContext): Promise<unknown> | unknown;
}

export class ZLinkConfigurationException extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'ZLinkConfigurationException';
  }
}

export function createFrameworkRegistration(
  options: ZLinkFrameworkRegistrationOptions = {}
): ZLinkFrameworkRegistration {
  const routeChannelOptions = toRouteChannelOptions(options);
  const registration: ZLinkFrameworkRegistration = {
    actorFactories: toTypeMap(options.actorFactories),
    spotFactories: new Set(options.spotFactories ?? []),
    channels: toChannelMap(options.channels),
    channelClients: channelNamesWith(options.channels, (channel) => channel.client !== undefined || channel.dealerMesh?.client !== undefined),
    fanoutPublishers: channelNamesWith(options.channels, (channel) => channel.publisher !== undefined),
    routeChannels: new Set(routeChannelOptions.keys()),
    routeChannelOptions,
    spotNodes: new Set(options.spotNodes ?? []),
    spotPublisherClients: new Set(options.spotPublisherClients ?? []),
    hasSpotRemoteAddressResolver: options.spotRemoteAddressResolver !== undefined,
    hasRegistrySpotRemoteAddresses: options.registrySpotRemoteAddresses !== undefined,
    spotRemoteAddressResolverType: options.spotRemoteAddressResolver,
    registrySpotRemoteAddresses: normalizeRegistrySpotRemoteAddresses(options.registrySpotRemoteAddresses, options.discovery)
  };
  validateFrameworkRegistration(registration, options);
  return registration;
}

function toChannelMap(channels: ZLinkFrameworkRegistrationOptions['channels']): Map<string, ZLinkChannelOptions> {
  return new Map(Object.entries(channels ?? {}).map(([name, channel]) => [name, { ...channel }]));
}

function toRouteChannelOptions(
  options: ZLinkFrameworkRegistrationOptions
): Map<string, ZLinkRouteChannelOptions> {
  const routeOptions = new Map<string, ZLinkRouteChannelOptions>();
  for (const routeChannel of options.routeChannels ?? []) {
    if (typeof routeChannel === 'string') {
      routeOptions.set(routeChannel, { routerChannelId: routeChannel });
      continue;
    }
    routeOptions.set(routeChannel.routerChannelId, { ...routeChannel });
  }
  for (const [channelName, channel] of Object.entries(options.channels ?? {})) {
    if (channel.routeMesh === undefined) {
      continue;
    }
    if (routeOptions.has(channelName)) {
      throw new ZLinkConfigurationException(`Route mesh channel '${channelName}' is already registered.`);
    }
    routeOptions.set(channelName, {
      routerChannelId: channelName,
      ...channel.routeMesh
    });
  }
  return routeOptions;
}

export function validateFrameworkRegistration(
  registration: ZLinkFrameworkRegistration,
  options: ZLinkFrameworkRegistrationOptions = {}
): void {
  if (registration.actorFactories.size > 0 && registration.spotNodes.size === 0) {
    throw new ZLinkConfigurationException('Actor factory registration requires at least one SpotNode.');
  }

  if (registration.hasRegistrySpotRemoteAddresses && registration.routeChannels.size === 0) {
    throw new ZLinkConfigurationException('Registry remote address resolver requires a route mesh channel.');
  }

  if (registration.hasRegistrySpotRemoteAddresses && registration.hasSpotRemoteAddressResolver) {
    throw new ZLinkConfigurationException('SPOT remote address resolver is already registered.');
  }

  if (registration.hasRegistrySpotRemoteAddresses && !hasDiscovery(options.discovery)) {
    throw new ZLinkConfigurationException(
      'Registry remote address resolver requires discovery endpoints from discovery.registries.'
    );
  }

  validateRegistryRouteChannel(registration);
  validateChannelCapabilities(options.channels, hasDiscovery(options.discovery));
  validateRouteChannels(registration.routeChannelOptions);
}

export function hasSpotNode(registration: ZLinkFrameworkRegistration): boolean {
  return registration.spotNodes.size > 0;
}

export function hasActorManager(registration: ZLinkFrameworkRegistration): boolean {
  return hasSpotNode(registration) && registration.actorFactories.size > 0;
}

export function hasSpotPublisherClient(registration: ZLinkFrameworkRegistration): boolean {
  return registration.spotPublisherClients.size > 0;
}

export function hasSpotRemoteAddressResolver(registration: ZLinkFrameworkRegistration): boolean {
  return registration.hasSpotRemoteAddressResolver || registration.hasRegistrySpotRemoteAddresses;
}

function toTypeMap(value: ZLinkFrameworkRegistrationOptions['actorFactories']): Map<string, Type> {
  if (value === undefined) {
    return new Map();
  }
  if (value instanceof Map) {
    return new Map(value);
  }
  return new Map(Object.entries(value));
}

function channelNamesWith(
  channels: ZLinkFrameworkRegistrationOptions['channels'],
  predicate: (channel: ZLinkChannelOptions) => boolean
): Set<string> {
  const names = new Set<string>();
  for (const [name, channel] of Object.entries(channels ?? {})) {
    if (predicate(channel)) {
      names.add(name);
    }
  }
  return names;
}

function validateChannelCapabilities(
  channels: ZLinkFrameworkRegistrationOptions['channels'],
  discoveryConfigured: boolean
): void {
  for (const [channelName, channel] of Object.entries(channels ?? {})) {
    if (channel.server !== undefined) {
      requireEndpoint(`channel '${channelName}' server`, channel.server.bind);
    }
    if (channel.publisher !== undefined) {
      requireEndpoint(`channel '${channelName}' publisher`, channel.publisher.bind);
    }
    if (channel.client !== undefined) {
      requirePeerSource(`channel '${channelName}' client`, channel.client.manualConnections, discoveryConfigured);
    }
    if (channel.dealerMesh !== undefined) {
      if (channel.client !== undefined) {
        throw new ZLinkConfigurationException(`Channel '${channelName}' cannot define both client and dealerMesh client capability.`);
      }
      if (channel.dealerMesh.bind !== undefined) {
        requireEndpoint(`channel '${channelName}' dealer mesh`, channel.dealerMesh.bind);
      }
      requirePeerSource(`channel '${channelName}' dealer mesh client`, channel.dealerMesh.client?.manualConnections, discoveryConfigured);
    }
    if (channel.subscriber !== undefined) {
      requirePeerSource(`channel '${channelName}' subscriber`, channel.subscriber.manualConnections, discoveryConfigured);
    }
    if (channel.routeMesh !== undefined) {
      requireEndpoint(`channel '${channelName}' route mesh`, channel.routeMesh.bind);
      if ((channel.routeMesh.manualConnections ?? []).some((endpoint) => endpoint.trim().length === 0)) {
        throw new ZLinkConfigurationException(`channel '${channelName}' route mesh manual connection endpoint must not be empty.`);
      }
    }
  }
}

function hasDiscovery(discovery: ZLinkDiscoveryOptions | undefined): boolean {
  return (discovery?.registries ?? []).some((endpoint) => endpoint.trim().length > 0);
}

function normalizeRegistrySpotRemoteAddresses(
  value: ZLinkFrameworkRegistrationOptions['registrySpotRemoteAddresses'],
  discovery: ZLinkDiscoveryOptions | undefined
): ZLinkRegistrySpotRemoteAddressesRegistration | undefined {
  if (value === undefined) {
    return undefined;
  }
  if (value.namespace.trim().length === 0 || value.namespace.trim() !== value.namespace) {
    throw new ZLinkConfigurationException('Registry route namespace must not be empty or padded.');
  }
  if (value.routerChannelId !== undefined && (value.routerChannelId.trim().length === 0 || value.routerChannelId.trim() !== value.routerChannelId)) {
    throw new ZLinkConfigurationException('Registry route RouterChannelId must not be empty or padded.');
  }
  const registryEndpoint = (discovery?.registries ?? []).find((endpoint) => endpoint.trim().length > 0);
  return {
    namespace: value.namespace,
    routerChannelId: value.routerChannelId,
    registryEndpoint: registryEndpoint ?? ''
  };
}

function validateRegistryRouteChannel(registration: ZLinkFrameworkRegistration): void {
  if (!registration.hasRegistrySpotRemoteAddresses) {
    return;
  }

  const routerChannelId = registration.registrySpotRemoteAddresses?.routerChannelId;
  if (routerChannelId !== undefined) {
    if (!registration.routeChannels.has(routerChannelId)) {
      throw new ZLinkConfigurationException(
        `Registry SPOT remote address resolver references unknown route mesh channel '${routerChannelId}'.`
      );
    }
    return;
  }

  if (registration.routeChannels.size > 1) {
    throw new ZLinkConfigurationException(
      'Registry SPOT remote address resolver requires RouterChannelId when more than one route mesh channel is registered.'
    );
  }
}

function requirePeerSource(
  capabilityName: string,
  manualConnections: readonly string[] | undefined,
  discoveryConfigured: boolean
): void {
  if ((manualConnections ?? []).some((endpoint) => endpoint.trim().length === 0)) {
    throw new ZLinkConfigurationException(`${capabilityName} manual connection endpoint must not be empty.`);
  }
  if ((manualConnections ?? []).length > 0 || discoveryConfigured) {
    return;
  }
  throw new ZLinkConfigurationException(`${capabilityName} requires discovery or manual connections.`);
}

function requireEndpoint(capabilityName: string, endpoint: string | undefined): void {
  if (endpoint === undefined || endpoint.trim().length === 0) {
    throw new ZLinkConfigurationException(`${capabilityName} must define a bind endpoint.`);
  }
}

function validateRouteChannels(routeChannels: ReadonlyMap<string, ZLinkRouteChannelOptions>): void {
  for (const routeChannel of routeChannels.values()) {
    if (routeChannelHandlerCount(routeChannel) > 0) {
      requireEndpoint(`route channel '${routeChannel.routerChannelId}' router`, routeChannel.bind);
    }
  }
}

function routeChannelHandlerCount(routeChannel: ZLinkRouteChannelOptions): number {
  return (routeChannel.handlers ?? []).length +
    (routeChannel.sendHandlers ?? []).length +
    (routeChannel.requestHandlers ?? []).length;
}
