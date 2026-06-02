import type {
  ChannelClientCapabilityBuilder,
  ChannelPublisherCapabilityBuilder,
  ChannelServerCapabilityBuilder,
  ChannelSubscriberCapabilityBuilder,
  DealerMeshChannelClientCapabilityBuilder,
  SpotChannelClientCapabilityBuilder,
  SpotPubSubCapabilityBuilder,
  SpotPublisherClientCapabilityBuilder,
  SpotRouterCapabilityBuilder,
  RoutingId,
  Type,
  ZLinkClientServerChannelBuilder,
  ZLinkDealerMeshChannelBuilder,
  ZLinkDiscoveryBuilder,
  ZLinkFanoutChannelBuilder,
  ZLinkFrameworkOptions,
  ZLinkRouteChannelBuilder,
  ZLinkRouteMeshChannelBuilder,
  ZLinkRouteRequestContext,
  ZLinkRouteSendContext,
  ZLinkSpot,
  ZLinkSpotMeshBuilder,
  ZLinkSpotMeshNodeBuilder,
  ZLinkSpotNodeBuilder,
  ZLinkSpotRouteChannelAcceptanceBuilder,
  ZLinkStreamNodeBuilder,
  ZLinkSession
} from '../../contracts';

export interface ZLinkFrameworkRegistration {
  readonly actorFactories: ReadonlyMap<string, Type>;
  readonly spotFactories: ReadonlySet<Type<ZLinkSpot>>;
  readonly channels: ReadonlyMap<string, ZLinkChannelOptions>;
  readonly channelClients: ReadonlySet<string>;
  readonly fanoutPublishers: ReadonlySet<string>;
  readonly routeChannels: ReadonlySet<string>;
  readonly routeChannelOptions: ReadonlyMap<string, ZLinkRouteChannelOptions>;
  readonly streamNodes: ReadonlyMap<string, ZLinkStreamNodeOptions>;
  readonly spotNodes: ReadonlyMap<string, ZLinkSpotNodeOptions>;
  readonly discovery?: ZLinkDiscoveryOptions;
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
  readonly streamNodes?: Readonly<Record<string, ZLinkStreamNodeOptions>>;
  readonly spotNodes?: readonly (string | ZLinkSpotNodeRegistrationOptions)[] |
    Readonly<Record<string, ZLinkSpotNodeOptions>>;
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

export interface ZLinkStreamNodeOptions {
  readonly bind?: string;
  readonly attachActorGateway?: string;
  readonly session?: Type;
}

export interface ZLinkSpotNodeRegistrationOptions extends ZLinkSpotNodeOptions {
  readonly name: string;
}

export interface ZLinkSpotNodeOptions {
  readonly router?: ZLinkSpotRouterCapabilityOptions;
  readonly pubSub?: ZLinkSpotPubSubCapabilityOptions;
  readonly attachedChannelClients?: Readonly<Record<string, ZLinkSpotAttachedChannelClientOptions>>;
  readonly attachedSpotPublisherClients?: Readonly<Record<string, ZLinkSpotPublisherClientOptions>>;
  readonly acceptedSpotRouteChannels?: Readonly<Record<string, ZLinkSpotRouteChannelAcceptanceOptions>>;
}

export interface ZLinkSpotRouterCapabilityOptions {
  readonly bind?: string;
  readonly manualConnections?: readonly string[];
  readonly routingId?: string;
}

export interface ZLinkSpotPubSubCapabilityOptions {
  readonly bind?: string;
  readonly manualConnections?: readonly string[];
  readonly routingId?: string;
}

export interface ZLinkSpotAttachedChannelClientOptions {
  readonly manualConnections?: readonly string[];
}

export interface ZLinkSpotPublisherClientOptions {
  readonly manualConnections?: readonly string[];
}

export interface ZLinkSpotRouteChannelAcceptanceOptions {
  readonly manualConnections?: readonly string[];
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
  const spotNodes = toSpotNodeMap(options.spotNodes);
  const registration: ZLinkFrameworkRegistration = {
    actorFactories: toTypeMap(options.actorFactories),
    spotFactories: new Set(options.spotFactories ?? []),
    channels: toChannelMap(options.channels),
    channelClients: channelNamesWith(options.channels, (channel) => channel.client !== undefined || channel.dealerMesh?.client !== undefined),
    fanoutPublishers: channelNamesWith(options.channels, (channel) => channel.publisher !== undefined),
    routeChannels: new Set(routeChannelOptions.keys()),
    routeChannelOptions,
    streamNodes: toStreamNodeMap(options.streamNodes),
    spotNodes,
    discovery: options.discovery,
    spotPublisherClients: toSpotPublisherClientSet(options.spotPublisherClients, spotNodes),
    hasSpotRemoteAddressResolver: options.spotRemoteAddressResolver !== undefined,
    hasRegistrySpotRemoteAddresses: options.registrySpotRemoteAddresses !== undefined,
    spotRemoteAddressResolverType: options.spotRemoteAddressResolver,
    registrySpotRemoteAddresses: normalizeRegistrySpotRemoteAddresses(options.registrySpotRemoteAddresses, options.discovery)
  };
  validateFrameworkRegistration(registration, options);
  return registration;
}

export function createFrameworkOptions(
  configure: (options: ZLinkFrameworkOptions) => void
): ZLinkFrameworkRegistrationOptions {
  const builder = new ZLinkFrameworkOptionsBuilder();
  configure(builder);
  return builder.build();
}

export function createFrameworkRegistrationWithBuilder(
  configure: (options: ZLinkFrameworkOptions) => void
): ZLinkFrameworkRegistration {
  return createFrameworkRegistration(createFrameworkOptions(configure));
}

class ZLinkFrameworkOptionsBuilder implements ZLinkFrameworkOptions {
  private readonly spotMeshes = new Set<string>();
  private readonly options: MutableFrameworkRegistrationOptions = {
    channels: {},
    discovery: { registries: [] },
    routeChannels: [],
    streamNodes: {},
    spotNodes: {},
    spotFactories: []
  };

  useDiscovery(): ZLinkDiscoveryBuilder {
    return new DefaultDiscoveryBuilder(this.options.discovery);
  }

  spotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this {
    this.options.spotFactories.push(spotType);
    return this;
  }

  addSpotMesh(channelName: string): ZLinkSpotMeshBuilder {
    if (channelName.trim().length === 0 || channelName.trim() !== channelName) {
      throw new ZLinkConfigurationException('SPOT mesh channel name must not be empty or padded.');
    }
    if (this.spotMeshes.has(channelName)) {
      throw new ZLinkConfigurationException(`Duplicate SPOT mesh channel '${channelName}'.`);
    }
    this.spotMeshes.add(channelName);
    return new DefaultSpotMeshBuilder(this);
  }

  clientServerChannel(name: string): ZLinkClientServerChannelBuilder {
    return new DefaultClientServerChannelBuilder(this.channel(name));
  }

  fanoutChannel(name: string): ZLinkFanoutChannelBuilder {
    return new DefaultFanoutChannelBuilder(this.channel(name));
  }

  dealerMeshChannel(name: string): ZLinkDealerMeshChannelBuilder {
    return new DefaultDealerMeshChannelBuilder(this.channel(name));
  }

  routeChannel(name: string): ZLinkRouteChannelBuilder {
    const routeChannel: MutableRouteChannelOptions = { routerChannelId: name };
    this.options.routeChannels.push(routeChannel);
    return new DefaultRouteChannelBuilder(routeChannel);
  }

  routeMeshChannel(name: string): ZLinkRouteMeshChannelBuilder {
    const channel = this.channel(name);
    channel.routeMesh ??= {};
    return new DefaultRouteMeshChannelBuilder(channel.routeMesh);
  }

  streamNode(name: string): ZLinkStreamNodeBuilder {
    const streamNode = this.streamNodeOptions(name);
    return new DefaultStreamNodeBuilder(streamNode);
  }

  spotNode(name: string): ZLinkSpotNodeBuilder {
    const spotNode = this.spotNodeOptions(name);
    return new DefaultSpotNodeBuilder(spotNode);
  }

  build(): ZLinkFrameworkRegistrationOptions {
    const discovery = this.options.discovery.registries.length === 0
      ? undefined
      : { registries: [...this.options.discovery.registries] };
    return {
      channels: this.options.channels,
      discovery,
      routeChannels: this.options.routeChannels,
      streamNodes: this.options.streamNodes,
      spotNodes: this.options.spotNodes,
      spotFactories: this.options.spotFactories
    };
  }

  private channel(name: string): MutableChannelOptions {
    this.options.channels[name] ??= {};
    return this.options.channels[name];
  }

  private streamNodeOptions(name: string): MutableStreamNodeOptions {
    this.options.streamNodes[name] ??= {};
    return this.options.streamNodes[name];
  }

  private spotNodeOptions(name: string): MutableSpotNodeOptions {
    this.options.spotNodes[name] ??= {};
    return this.options.spotNodes[name];
  }
}

class DefaultDiscoveryBuilder implements ZLinkDiscoveryBuilder {
  constructor(private readonly discovery: MutableDiscoveryOptions) {}

  connectRegistry(endpoint: string): this {
    this.discovery.registries.push(endpoint);
    return this;
  }
}

class DefaultClientServerChannelBuilder implements ZLinkClientServerChannelBuilder {
  constructor(private readonly channel: MutableChannelOptions) {}

  server(): ChannelServerCapabilityBuilder {
    this.channel.server ??= {};
    return new DefaultBindCapabilityBuilder(this.channel.server);
  }

  client(): ChannelClientCapabilityBuilder {
    this.channel.client ??= { manualConnections: [] };
    return new DefaultConnectionCapabilityBuilder(this.channel.client);
  }
}

class DefaultFanoutChannelBuilder implements ZLinkFanoutChannelBuilder {
  constructor(private readonly channel: MutableChannelOptions) {}

  publisher(): ChannelPublisherCapabilityBuilder {
    this.channel.publisher ??= {};
    return new DefaultBindCapabilityBuilder(this.channel.publisher);
  }

  subscriber(): ChannelSubscriberCapabilityBuilder {
    this.channel.subscriber ??= { manualConnections: [] };
    return new DefaultConnectionCapabilityBuilder(this.channel.subscriber);
  }
}

class DefaultDealerMeshChannelBuilder implements ZLinkDealerMeshChannelBuilder {
  constructor(private readonly channel: MutableChannelOptions) {}

  client(): DealerMeshChannelClientCapabilityBuilder {
    this.channel.dealerMesh ??= {};
    this.channel.dealerMesh.client ??= { manualConnections: [] };
    return new DefaultConnectionCapabilityBuilder(this.channel.dealerMesh.client);
  }
}

class DefaultRouteChannelBuilder implements ZLinkRouteChannelBuilder {
  constructor(private readonly routeChannel: MutableRouteChannelOptions) {}

  router(): ChannelServerCapabilityBuilder {
    return new DefaultRouteBindCapabilityBuilder(this.routeChannel);
  }

  dealer(): ChannelClientCapabilityBuilder {
    return new DefaultRouteConnectionCapabilityBuilder(this.routeChannel);
  }
}

class DefaultRouteMeshChannelBuilder implements ZLinkRouteMeshChannelBuilder {
  constructor(private readonly routeMesh: MutableRouteMeshChannelOptions) {}

  router(): ChannelServerCapabilityBuilder {
    return new DefaultRouteBindCapabilityBuilder(this.routeMesh);
  }

  dealer(): ChannelClientCapabilityBuilder {
    return new DefaultRouteConnectionCapabilityBuilder(this.routeMesh);
  }
}

class DefaultStreamNodeBuilder implements ZLinkStreamNodeBuilder {
  constructor(private readonly streamNode: MutableStreamNodeOptions) {}

  bind(endpoint: string): this {
    this.streamNode.bind = endpoint;
    return this;
  }

  attachActorGateway(spotNodeName: string): this {
    this.streamNode.attachActorGateway = spotNodeName;
    return this;
  }

  registerSession<TSession extends ZLinkSession>(sessionType: Type<TSession>): this {
    this.streamNode.session = sessionType;
    return this;
  }
}

class DefaultSpotMeshBuilder implements ZLinkSpotMeshBuilder {
  constructor(private readonly root: ZLinkFrameworkOptionsBuilder) {}

  useDiscovery(): ZLinkDiscoveryBuilder {
    return this.root.useDiscovery();
  }

  node(name: string): ZLinkSpotMeshNodeBuilder {
    return this.root.spotNode(name);
  }
}

class DefaultSpotNodeBuilder implements ZLinkSpotNodeBuilder {
  constructor(private readonly spotNode: MutableSpotNodeOptions) {}

  router(): SpotRouterCapabilityBuilder {
    this.spotNode.router ??= { manualConnections: [] };
    return new DefaultSpotRouterCapabilityBuilder(this.spotNode.router);
  }

  pubSub(): SpotPubSubCapabilityBuilder {
    this.spotNode.pubSub ??= { manualConnections: [] };
    return new DefaultSpotPubSubCapabilityBuilder(this.spotNode.pubSub);
  }

  attachChannelClient(channelName: string): SpotChannelClientCapabilityBuilder {
    this.spotNode.attachedChannelClients ??= {};
    this.spotNode.attachedChannelClients[channelName] ??= { manualConnections: [] };
    return new DefaultConnectionCapabilityBuilder(this.spotNode.attachedChannelClients[channelName]);
  }

  attachSpotPublisherClient(channelName: string): SpotPublisherClientCapabilityBuilder {
    this.spotNode.attachedSpotPublisherClients ??= {};
    this.spotNode.attachedSpotPublisherClients[channelName] ??= { manualConnections: [] };
    return new DefaultConnectionCapabilityBuilder(this.spotNode.attachedSpotPublisherClients[channelName]);
  }

  acceptSpotRoutesFromChannel(channelName: string): ZLinkSpotRouteChannelAcceptanceBuilder {
    this.spotNode.acceptedSpotRouteChannels ??= {};
    this.spotNode.acceptedSpotRouteChannels[channelName] ??= { manualConnections: [] };
    return new DefaultConnectionCapabilityBuilder(this.spotNode.acceptedSpotRouteChannels[channelName]);
  }
}

class DefaultSpotRouterCapabilityBuilder implements SpotRouterCapabilityBuilder {
  constructor(private readonly router: MutableSpotRouterCapabilityOptions) {}

  bind(endpoint: string): this {
    this.router.bind = endpoint;
    return this;
  }

  routingId(routingId: RoutingId): this {
    this.router.routingId = routingId;
    return this;
  }

  connect(endpoint: string): this {
    this.router.manualConnections ??= [];
    this.router.manualConnections.push(endpoint);
    return this;
  }
}

class DefaultSpotPubSubCapabilityBuilder implements SpotPubSubCapabilityBuilder {
  constructor(private readonly pubSub: MutableSpotPubSubCapabilityOptions) {}

  bind(endpoint: string): this {
    this.pubSub.bind = endpoint;
    return this;
  }

  routingId(routingId: RoutingId): this {
    this.pubSub.routingId = routingId;
    return this;
  }

  connect(endpoint: string): this {
    this.pubSub.manualConnections ??= [];
    this.pubSub.manualConnections.push(endpoint);
    return this;
  }
}

class DefaultBindCapabilityBuilder implements ChannelServerCapabilityBuilder, ChannelPublisherCapabilityBuilder {
  constructor(private readonly capability: { bind?: string }) {}

  bind(endpoint: string): this {
    this.capability.bind = endpoint;
    return this;
  }
}

class DefaultConnectionCapabilityBuilder implements
  ChannelClientCapabilityBuilder,
  ChannelSubscriberCapabilityBuilder,
  DealerMeshChannelClientCapabilityBuilder,
  SpotChannelClientCapabilityBuilder,
  SpotPublisherClientCapabilityBuilder,
  ZLinkSpotRouteChannelAcceptanceBuilder {
  constructor(private readonly capability: { manualConnections?: string[] }) {}

  connect(endpoint: string): this {
    this.capability.manualConnections ??= [];
    this.capability.manualConnections.push(endpoint);
    return this;
  }
}

class DefaultRouteBindCapabilityBuilder implements ChannelServerCapabilityBuilder {
  constructor(private readonly capability: { bind?: string }) {}

  bind(endpoint: string): this {
    this.capability.bind = endpoint;
    return this;
  }
}

class DefaultRouteConnectionCapabilityBuilder implements ChannelClientCapabilityBuilder {
  constructor(private readonly capability: { manualConnections?: string[] }) {}

  connect(endpoint: string): this {
    this.capability.manualConnections ??= [];
    this.capability.manualConnections.push(endpoint);
    return this;
  }
}

interface MutableFrameworkRegistrationOptions {
  channels: Record<string, MutableChannelOptions>;
  discovery: MutableDiscoveryOptions;
  routeChannels: MutableRouteChannelOptions[];
  streamNodes: Record<string, MutableStreamNodeOptions>;
  spotNodes: Record<string, MutableSpotNodeOptions>;
  spotFactories: Type<ZLinkSpot>[];
}

interface MutableDiscoveryOptions {
  registries: string[];
}

interface MutableChannelOptions {
  client?: MutableClientCapabilityOptions;
  dealerMesh?: MutableDealerMeshChannelOptions;
  publisher?: MutablePublisherCapabilityOptions;
  routeMesh?: MutableRouteMeshChannelOptions;
  server?: { bind?: string };
  subscriber?: MutableClientCapabilityOptions;
}

interface MutableClientCapabilityOptions {
  manualConnections?: string[];
}

interface MutableDealerMeshChannelOptions {
  bind?: string;
  client?: MutableClientCapabilityOptions;
}

interface MutablePublisherCapabilityOptions {
  bind?: string;
}

interface MutableRouteMeshChannelOptions {
  bind?: string;
  manualConnections?: string[];
  routingId?: string;
  sendHandlers?: ZLinkRouteChannelSendHandlerRegistration[];
  requestHandlers?: ZLinkRouteChannelRequestHandlerRegistration[];
  handlers?: ZLinkRouteChannelHandlerOptions[];
}

interface MutableRouteChannelOptions extends MutableRouteMeshChannelOptions {
  routerChannelId: string;
}

interface MutableStreamNodeOptions {
  bind?: string;
  attachActorGateway?: string;
  session?: Type;
}

interface MutableSpotNodeOptions {
  router?: MutableSpotRouterCapabilityOptions;
  pubSub?: MutableSpotPubSubCapabilityOptions;
  attachedChannelClients?: Record<string, MutableSpotAttachedChannelClientOptions>;
  attachedSpotPublisherClients?: Record<string, MutableSpotPublisherClientOptions>;
  acceptedSpotRouteChannels?: Record<string, MutableSpotRouteChannelAcceptanceOptions>;
}

interface MutableSpotRouterCapabilityOptions {
  bind?: string;
  manualConnections?: string[];
  routingId?: string;
}

interface MutableSpotPubSubCapabilityOptions {
  bind?: string;
  manualConnections?: string[];
  routingId?: string;
}

interface MutableSpotAttachedChannelClientOptions {
  manualConnections?: string[];
}

interface MutableSpotPublisherClientOptions {
  manualConnections?: string[];
}

interface MutableSpotRouteChannelAcceptanceOptions {
  manualConnections?: string[];
}

function toChannelMap(channels: ZLinkFrameworkRegistrationOptions['channels']): Map<string, ZLinkChannelOptions> {
  return new Map(Object.entries(channels ?? {}).map(([name, channel]) => [name, { ...channel }]));
}

function toStreamNodeMap(streamNodes: ZLinkFrameworkRegistrationOptions['streamNodes']): Map<string, ZLinkStreamNodeOptions> {
  return new Map(Object.entries(streamNodes ?? {}).map(([name, streamNode]) => [name, { ...streamNode }]));
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
  validateSpotNodes(registration);
  validateRouteChannels(registration.routeChannelOptions);
  validateStreamNodes(registration);
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

function toSpotNodeMap(value: ZLinkFrameworkRegistrationOptions['spotNodes']): Map<string, ZLinkSpotNodeOptions> {
  if (value === undefined) {
    return new Map();
  }
  if (!Array.isArray(value)) {
    return new Map(Object.entries(value).map(([name, spotNode]) => [name, { ...spotNode }]));
  }
  return new Map(value.map((spotNode) => {
    if (typeof spotNode === 'string') {
      return [spotNode, {}];
    }
    const { name, ...options } = spotNode;
    return [name, options];
  }));
}

function toSpotPublisherClientSet(
  explicitClients: readonly string[] | undefined,
  spotNodes: ReadonlyMap<string, ZLinkSpotNodeOptions>
): Set<string> {
  const clients = new Set(explicitClients ?? []);
  for (const spotNode of spotNodes.values()) {
    for (const channelName of Object.keys(spotNode.attachedSpotPublisherClients ?? {})) {
      clients.add(channelName);
    }
  }
  return clients;
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

function validateSpotNodes(registration: ZLinkFrameworkRegistration): void {
  for (const [spotNodeName, spotNode] of registration.spotNodes.entries()) {
    if (spotNodeName.trim().length === 0 || spotNodeName.trim() !== spotNodeName) {
      throw new ZLinkConfigurationException('SpotNode name must not be empty or padded.');
    }
    validateSpotNodeCapability(`SpotNode '${spotNodeName}' router`, spotNode.router);
    validateSpotNodeCapability(`SpotNode '${spotNodeName}' pubSub`, spotNode.pubSub);
    validateAttachedChannelClients(spotNodeName, spotNode, registration);
    validateAttachedSpotPublisherClients(spotNodeName, spotNode);
    validateAcceptedSpotRouteChannels(spotNodeName, spotNode, registration);
    if (
      spotNode.router?.routingId !== undefined &&
      spotNode.pubSub?.routingId !== undefined &&
      spotNode.router.routingId !== spotNode.pubSub.routingId
    ) {
      throw new ZLinkConfigurationException(
        `SpotNode '${spotNodeName}' router and pubSub routingId must match.`
      );
    }
  }
}

function validateAttachedChannelClients(
  spotNodeName: string,
  spotNode: ZLinkSpotNodeOptions,
  registration: ZLinkFrameworkRegistration
): void {
  for (const [channelName, attached] of Object.entries(spotNode.attachedChannelClients ?? {})) {
    requireName(`SpotNode '${spotNodeName}' attached channel client name`, channelName);
    if (registration.channels.get(channelName)?.server === undefined) {
      throw new ZLinkConfigurationException(
        `SpotNode '${spotNodeName}' attached channel client '${channelName}' must reference a client-server channel.`
      );
    }
    validateManualConnections(
      `SpotNode '${spotNodeName}' attached channel client '${channelName}'`,
      attached.manualConnections
    );
  }
}

function validateAttachedSpotPublisherClients(
  spotNodeName: string,
  spotNode: ZLinkSpotNodeOptions
): void {
  for (const [channelName, attached] of Object.entries(spotNode.attachedSpotPublisherClients ?? {})) {
    requireName(`SpotNode '${spotNodeName}' attached SPOT publisher channel name`, channelName);
    validateManualConnections(
      `SpotNode '${spotNodeName}' attached SPOT publisher '${channelName}'`,
      attached.manualConnections
    );
  }
}

function validateAcceptedSpotRouteChannels(
  spotNodeName: string,
  spotNode: ZLinkSpotNodeOptions,
  registration: ZLinkFrameworkRegistration
): void {
  for (const [channelName, acceptance] of Object.entries(spotNode.acceptedSpotRouteChannels ?? {})) {
    requireName(`SpotNode '${spotNodeName}' accepted SPOT route channel name`, channelName);
    validateManualConnections(
      `SpotNode '${spotNodeName}' accepted SPOT route channel '${channelName}'`,
      acceptance.manualConnections
    );
    if (registration.routeChannelOptions.has(channelName)) {
      continue;
    }
    if (registration.channels.get(channelName)?.server !== undefined) {
      continue;
    }
    throw new ZLinkConfigurationException(
      `Accepted SPOT route channel '${channelName}' is not router-capable.`
    );
  }
}

function validateSpotNodeCapability(
  capabilityName: string,
  capability: ZLinkSpotRouterCapabilityOptions | ZLinkSpotPubSubCapabilityOptions | undefined
): void {
  if (capability === undefined) {
    return;
  }
  if (capability.bind !== undefined) {
    requireEndpoint(capabilityName, capability.bind);
  }
  if ((capability.manualConnections ?? []).some((endpoint) => endpoint.trim().length === 0)) {
    throw new ZLinkConfigurationException(`${capabilityName} manual connection endpoint must not be empty.`);
  }
  if (capability.routingId !== undefined && (capability.routingId.trim().length === 0 || capability.routingId.trim() !== capability.routingId)) {
    throw new ZLinkConfigurationException(`${capabilityName} routingId must not be empty or padded.`);
  }
}

function validateManualConnections(capabilityName: string, manualConnections: readonly string[] | undefined): void {
  if ((manualConnections ?? []).some((endpoint) => endpoint.trim().length === 0)) {
    throw new ZLinkConfigurationException(`${capabilityName} manual connection endpoint must not be empty.`);
  }
}

function requireName(label: string, value: string): void {
  if (value.trim().length === 0 || value.trim() !== value) {
    throw new ZLinkConfigurationException(`${label} must not be empty or padded.`);
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

function validateStreamNodes(registration: ZLinkFrameworkRegistration): void {
  for (const [streamNodeName, streamNode] of registration.streamNodes.entries()) {
    requireEndpoint(`STREAM node '${streamNodeName}'`, streamNode.bind);
    if (streamNode.session === undefined) {
      throw new ZLinkConfigurationException(
        `STREAM node '${streamNodeName}' must register a header stream session.`
      );
    }
    if (streamNode.attachActorGateway === undefined || streamNode.attachActorGateway.trim().length === 0) {
      continue;
    }
    const spotNode = registration.spotNodes.get(streamNode.attachActorGateway);
    if (spotNode === undefined) {
      throw new ZLinkConfigurationException(
        `STREAM node '${streamNodeName}' references unknown ActorGateway target SpotNode '${streamNode.attachActorGateway}'.`
      );
    }
    if (spotNode.router === undefined) {
      throw new ZLinkConfigurationException(
        `STREAM node '${streamNodeName}' attaches ActorGateway to SpotNode '${streamNode.attachActorGateway}' but that SpotNode does not enable router capability.`
      );
    }
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
