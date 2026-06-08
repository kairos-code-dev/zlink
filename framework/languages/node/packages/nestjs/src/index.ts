import 'reflect-metadata';
import { createRequire } from 'node:module';
import path from 'node:path';
import { Module } from '@nestjs/common';
import type { DynamicModule, InjectionToken, ModuleMetadata, OnModuleDestroy, OnModuleInit, Provider } from '@nestjs/common';
import { DiscoveryModule, DiscoveryService, ModuleRef } from '@nestjs/core';
import type {
  Type,
  ZLinkChannelPublishHandlerRegistration,
  ZLinkChannelRequestHandlerRegistration,
  ZLinkClientCapabilityOptions,
  ZLinkDealerMeshChannelOptions,
  ZLinkChannelOptions,
  ZLinkFrameworkRegistration,
  ZLinkFrameworkRegistrationOptions,
  ZLinkProviderResolver,
  ZLinkPublisherCapabilityOptions,
  ZLinkPublishContext,
  ZLinkRequestContext,
  ZLinkRouteChannelOptions,
  ZLinkRouteChannelRequestHandlerRegistration,
  ZLinkRouteChannelSendHandlerRegistration,
  ZLinkRouteSendContext,
  ZLinkRegistryOptions,
  ZLinkRegistryQueryClientOptions,
  ZLinkSpotNodeRegistrationOptions,
  ZLinkSpotNodeOptions,
  ZLinkSpotRemoteAddressResolver,
  ZLinkStreamNodeOptions
} from '@zlink-systems/framework';

type FrameworkModule = typeof import('@zlink-systems/framework');

const framework = loadFramework();

type RuntimeHost = InstanceType<FrameworkModule['ZLinkFrameworkRuntimeHost']>;
type RuntimeHostWithNestLifecycle = RuntimeHost & OnModuleInit & OnModuleDestroy;

export interface ZLinkModuleAsyncOptions {
  readonly useFactory: (...args: unknown[]) => ZLinkModuleOptions | Promise<ZLinkModuleOptions>;
  readonly inject?: readonly InjectionToken[];
  readonly imports?: ModuleMetadata['imports'];
}

export interface ZLinkRegistryModuleAsyncOptions {
  readonly useFactory: (...args: unknown[]) => ZLinkRegistryOptions | Promise<ZLinkRegistryOptions>;
  readonly inject?: readonly InjectionToken[];
  readonly imports?: ModuleMetadata['imports'];
}

export interface ZLinkRegistryQueryClientModuleAsyncOptions {
  readonly useFactory: (...args: unknown[]) => ZLinkRegistryQueryClientOptions | Promise<ZLinkRegistryQueryClientOptions>;
  readonly inject?: readonly InjectionToken[];
  readonly imports?: ModuleMetadata['imports'];
}

interface ZLinkNestHandlerDiscoveryOptions {
  readonly handlerGroups?: readonly string[];
}

export interface ZLinkNestClientServerChannelOptions extends ZLinkNestHandlerDiscoveryOptions {
  readonly server?: { readonly bind?: string };
  readonly client?: ZLinkClientCapabilityOptions;
  readonly requestHandlers?: readonly ZLinkChannelRequestHandlerRegistration[];
}

export interface ZLinkNestFanoutChannelOptions extends ZLinkNestHandlerDiscoveryOptions {
  readonly publisher?: ZLinkPublisherCapabilityOptions;
  readonly subscriber?: ZLinkClientCapabilityOptions;
  readonly publishHandlers?: readonly ZLinkChannelPublishHandlerRegistration[];
}

export interface ZLinkNestDealerMeshChannelOptions extends ZLinkDealerMeshChannelOptions {}

export interface ZLinkNestRouterMeshOptions extends ZLinkNestHandlerDiscoveryOptions {
  readonly bind?: string;
  readonly manualConnections?: readonly string[];
  readonly routingId?: string;
  readonly sendHandlers?: readonly ZLinkRouteChannelSendHandlerRegistration[];
  readonly requestHandlers?: readonly ZLinkRouteChannelRequestHandlerRegistration[];
  readonly handlers?: ZLinkRouteChannelOptions['handlers'];
}

export type ZLinkNestHandlerKind = 'request' | 'send' | 'publish';

export interface ZLinkNestHandlerGroupOptions {
  readonly kind?: ZLinkNestHandlerKind;
  readonly packetName?: string;
  readonly methodName?: string;
}

export interface ZLinkNestHandlerOptions {
  readonly methodName?: string;
}

export interface ZLinkNestHandlerGroupProvider extends ZLinkNestHandlerGroupOptions {
  readonly provider: Provider;
  readonly handlerType?: Type;
  readonly handlerToken?: InjectionToken;
}

export type ZLinkNestHandlerGroupEntry =
  | Type
  | ZLinkNestHandlerGroupProvider;

export type ZLinkNestHandlerProvider =
  | Type
  | Provider
  | ZLinkNestHandlerGroupProvider;

export interface ZLinkNestHandlerGroupBuilder {
  request(handler: ZLinkNestHandlerProvider, packetName?: string, options?: ZLinkNestHandlerOptions): this;
  send(handler: ZLinkNestHandlerProvider, packetName?: string, options?: ZLinkNestHandlerOptions): this;
  publish(handler: ZLinkNestHandlerProvider, packetName?: string, options?: ZLinkNestHandlerOptions): this;
  providers(): Provider[];
}

export interface ZLinkModuleOptions extends Omit<
  ZLinkFrameworkRegistrationOptions,
  'channels' | 'routeChannels' | 'streamNodes' | 'spotNodes'
> {
  readonly clientServerChannels?: Readonly<Record<string, ZLinkNestClientServerChannelOptions>>;
  readonly fanoutChannels?: Readonly<Record<string, ZLinkNestFanoutChannelOptions>>;
  readonly dealerMeshChannels?: Readonly<Record<string, ZLinkNestDealerMeshChannelOptions>>;
  readonly routerMeshes?: Readonly<Record<string, ZLinkNestRouterMeshOptions>>;
  readonly spotNodes?: readonly (string | ZLinkSpotNodeRegistrationOptions)[] |
    Readonly<Record<string, ZLinkSpotNodeOptions>>;
  readonly streams?: Readonly<Record<string, ZLinkStreamNodeOptions>>;
}

export interface ZLinkNestFrameworkOptionsBuilder {
  options(options: ZLinkNestFrameworkAdditionalOptions): this;
  clientServerChannel(name: string, configure: (channel: ZLinkNestClientServerChannelBuilder) => void): this;
  fanoutChannel(name: string, configure: (channel: ZLinkNestFanoutChannelBuilder) => void): this;
  routerMesh(name: string, configure: (mesh: ZLinkNestRouterMeshBuilder) => void): this;
  build(): ZLinkModuleOptions;
}

export type ZLinkNestFrameworkAdditionalOptions = Omit<
  ZLinkModuleOptions,
  'clientServerChannels' | 'fanoutChannels' | 'routerMeshes'
>;

export interface ZLinkNestClientServerChannelBuilder {
  server(bind: string | undefined): this;
  client(endpoint?: string | readonly string[]): this;
  handlerGroup(groupName: string): this;
}

export interface ZLinkNestFanoutChannelBuilder {
  publisher(bind: string | undefined): this;
  subscriber(endpoint?: string | readonly string[]): this;
  handlerGroup(groupName: string): this;
}

export interface ZLinkNestRouterMeshBuilder {
  bind(endpoint: string | undefined): this;
  routingId(routingId: string | undefined): this;
  connect(endpoint: string | readonly string[] | undefined): this;
  handlerGroup(groupName: string): this;
}

export const ZLINK_NEST_HANDLER_GROUP = Symbol.for('@zlink-systems/nestjs:handler-group');
export const ZLINK_FRAMEWORK_REGISTRATION = Symbol.for('@zlink-systems/framework:registration');
export const ZLINK_FRAMEWORK_RUNTIME = Symbol.for('@zlink-systems/framework:runtime');
export const ZLINK_CHANNEL_CLIENT = Symbol.for('@zlink-systems/framework:channel-client');
export const ZLINK_ROUTE_CLIENT = Symbol.for('@zlink-systems/framework:route-client');
export const ZLINK_FANOUT_CLIENT = Symbol.for('@zlink-systems/framework:fanout-client');
export const ZLINK_BOUND_SESSION_FACTORY = Symbol.for('@zlink-systems/framework:bound-session-factory');
export const ZLINK_MESSAGE_METADATA_POLICY = Symbol.for('@zlink-systems/framework:message-metadata-policy');
export const ZLINK_SPOT_MANAGER = Symbol.for('@zlink-systems/framework:spot-manager');
export const ZLINK_SPOT_OUTBOUND = Symbol.for('@zlink-systems/framework:spot-outbound');
export const ZLINK_SPOT_PUBLISHER_CLIENT = Symbol.for('@zlink-systems/framework:spot-publisher-client');
export const ZLINK_ACTOR_MANAGER = Symbol.for('@zlink-systems/framework:actor-manager');
export const ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER = Symbol.for('@zlink-systems/framework:spot-remote-address-resolver');
export const ZLINK_REGISTRY_RUNTIME = Symbol.for('@zlink-systems/framework:registry-runtime');
export const ZLINK_REGISTRY_QUERY = Symbol.for('@zlink-systems/framework:registry-query');
export const ZLINK_REGISTRY_QUERY_CLIENT = Symbol.for('@zlink-systems/framework:registry-query-client');

const nestHandlerMetadataByToken = new Map<unknown, readonly ZLinkNestHandlerMetadata[]>();

export function zlinkFramework(): ZLinkNestFrameworkOptionsBuilder {
  return new DefaultZLinkNestFrameworkOptionsBuilder();
}

export function zlinkHandlers(groupName: string): ZLinkNestHandlerGroupBuilder {
  return new DefaultZLinkNestHandlerGroupBuilder(groupName);
}

class DefaultZLinkNestFrameworkOptionsBuilder implements ZLinkNestFrameworkOptionsBuilder {
  private additionalOptions: ZLinkNestFrameworkAdditionalOptions = {};
  private readonly clientServerChannels: Record<string, ZLinkNestClientServerChannelOptions> = {};
  private readonly fanoutChannels: Record<string, ZLinkNestFanoutChannelOptions> = {};
  private readonly routerMeshes: Record<string, ZLinkNestRouterMeshOptions> = {};

  options(options: ZLinkNestFrameworkAdditionalOptions): this {
    this.additionalOptions = { ...this.additionalOptions, ...options };
    return this;
  }

  clientServerChannel(name: string, configure: (channel: ZLinkNestClientServerChannelBuilder) => void): this {
    const channel = new DefaultZLinkNestClientServerChannelBuilder();
    configure(channel);
    this.clientServerChannels[name] = channel.build();
    return this;
  }

  fanoutChannel(name: string, configure: (channel: ZLinkNestFanoutChannelBuilder) => void): this {
    const channel = new DefaultZLinkNestFanoutChannelBuilder();
    configure(channel);
    this.fanoutChannels[name] = channel.build();
    return this;
  }

  routerMesh(name: string, configure: (mesh: ZLinkNestRouterMeshBuilder) => void): this {
    const mesh = new DefaultZLinkNestRouterMeshBuilder();
    configure(mesh);
    this.routerMeshes[name] = mesh.build();
    return this;
  }

  build(): ZLinkModuleOptions {
    return {
      ...this.additionalOptions,
      clientServerChannels: { ...this.clientServerChannels },
      fanoutChannels: { ...this.fanoutChannels },
      routerMeshes: { ...this.routerMeshes }
    };
  }
}

class DefaultZLinkNestClientServerChannelBuilder implements ZLinkNestClientServerChannelBuilder {
  private options: ZLinkNestClientServerChannelOptions = {};

  server(bind: string | undefined): this {
    this.options = { ...this.options, server: { bind } };
    return this;
  }

  client(endpoint?: string | readonly string[]): this {
    this.options = { ...this.options, client: endpoint === undefined ? {} : { manualConnections: endpointList(endpoint) } };
    return this;
  }

  handlerGroup(groupName: string): this {
    this.options = { ...this.options, handlerGroups: [...(this.options.handlerGroups ?? []), groupName] };
    return this;
  }

  build(): ZLinkNestClientServerChannelOptions {
    return this.options;
  }
}

class DefaultZLinkNestFanoutChannelBuilder implements ZLinkNestFanoutChannelBuilder {
  private options: ZLinkNestFanoutChannelOptions = {};

  publisher(bind: string | undefined): this {
    this.options = { ...this.options, publisher: { bind } };
    return this;
  }

  subscriber(endpoint?: string | readonly string[]): this {
    this.options = { ...this.options, subscriber: endpoint === undefined ? {} : { manualConnections: endpointList(endpoint) } };
    return this;
  }

  handlerGroup(groupName: string): this {
    this.options = { ...this.options, handlerGroups: [...(this.options.handlerGroups ?? []), groupName] };
    return this;
  }

  build(): ZLinkNestFanoutChannelOptions {
    return this.options;
  }
}

class DefaultZLinkNestRouterMeshBuilder implements ZLinkNestRouterMeshBuilder {
  private options: ZLinkNestRouterMeshOptions = {};

  bind(endpoint: string | undefined): this {
    this.options = { ...this.options, bind: endpoint };
    return this;
  }

  routingId(routingId: string | undefined): this {
    this.options = { ...this.options, routingId };
    return this;
  }

  connect(endpoint: string | readonly string[] | undefined): this {
    this.options = { ...this.options, manualConnections: endpoint === undefined ? [] : endpointList(endpoint) };
    return this;
  }

  handlerGroup(groupName: string): this {
    this.options = { ...this.options, handlerGroups: [...(this.options.handlerGroups ?? []), groupName] };
    return this;
  }

  build(): ZLinkNestRouterMeshOptions {
    return this.options;
  }
}

function endpointList(endpoint: string | readonly string[]): string[] {
  return typeof endpoint === 'string' ? [endpoint] : [...endpoint];
}

class DefaultZLinkNestHandlerGroupBuilder implements ZLinkNestHandlerGroupBuilder {
  private readonly registeredProviders: Provider[] = [];

  constructor(private readonly groupName: string) {
    validateHandlerGroupName(groupName);
  }

  request(handler: ZLinkNestHandlerProvider, packetName?: string, options: ZLinkNestHandlerOptions = {}): this {
    return this.add('request', handler, packetName, options);
  }

  send(handler: ZLinkNestHandlerProvider, packetName?: string, options: ZLinkNestHandlerOptions = {}): this {
    return this.add('send', handler, packetName, options);
  }

  publish(handler: ZLinkNestHandlerProvider, packetName?: string, options: ZLinkNestHandlerOptions = {}): this {
    return this.add('publish', handler, packetName, options);
  }

  providers(): Provider[] {
    return [...this.registeredProviders];
  }

  private add(
    kind: ZLinkNestHandlerKind,
    handler: ZLinkNestHandlerProvider,
    packetName: string | undefined,
    options: ZLinkNestHandlerOptions
  ): this {
    this.registeredProviders.push(createHandlerGroupProvider(this.groupName, normalizeHandlerProvider(handler), {
      kind,
      methodName: options.methodName,
      packetName
    }));
    return this;
  }
}

function validateHandlerGroupName(groupName: string): void {
  if (groupName.trim() === '') {
    throw new framework.ZLinkConfigurationException('ZLink handler group name must not be empty.');
  }
}

function normalizeHandlerProvider(handler: ZLinkNestHandlerProvider): ZLinkNestHandlerGroupEntry {
  if (typeof handler === 'function') {
    return handler;
  }
  if (typeof handler === 'object' && handler !== null && 'provider' in handler) {
    return handler as ZLinkNestHandlerGroupProvider;
  }
  return { provider: handler as Provider };
}

interface ZLinkNestHandlerMetadata {
  readonly groupName: string;
  readonly kind: ZLinkNestHandlerKind;
  readonly packetName: string;
  readonly methodName: string;
}

function createHandlerGroupProvider(
  groupName: string,
  entry: ZLinkNestHandlerGroupEntry,
  defaults: ZLinkNestHandlerGroupOptions
): Provider {
  const normalized = normalizeHandlerGroupEntry(entry);
  const handlerToken = normalized.handlerToken ?? normalized.handlerType ?? resolveHandlerToken(normalized.provider);
  if (handlerToken === undefined) {
    throw new framework.ZLinkConfigurationException(
      `ZLink handler group '${groupName}' provider must expose a handler token.`
    );
  }
  const packetSource = normalized.handlerType ?? (typeof handlerToken === 'function' ? handlerToken as Type : undefined);
  appendNestHandlerMetadata(handlerToken, {
    groupName,
    kind: normalized.kind ?? defaults.kind ?? 'request',
    methodName: normalized.methodName ?? defaults.methodName ?? 'handle',
    packetName: normalized.packetName ?? defaults.packetName ?? inferPacketName(packetSource, handlerToken)
  });
  return normalized.provider;
}

function normalizeHandlerGroupEntry(entry: ZLinkNestHandlerGroupEntry): ZLinkNestHandlerGroupProvider {
  if (typeof entry === 'function') {
    return { provider: entry, handlerType: entry };
  }
  return entry as ZLinkNestHandlerGroupProvider;
}

function resolveHandlerToken(provider: Provider): InjectionToken | undefined {
  if (typeof provider === 'function') {
    return provider;
  }
  if (typeof provider === 'object' && provider !== null) {
    const typedProvider = provider as {
      readonly provide?: unknown;
      readonly useClass?: unknown;
    };
    if (typeof typedProvider.useClass === 'function') {
      return typedProvider.useClass as Type;
    }
    if (
      typeof typedProvider.provide === 'function' ||
      typeof typedProvider.provide === 'string' ||
      typeof typedProvider.provide === 'symbol'
    ) {
      return typedProvider.provide as InjectionToken;
    }
  }
  return undefined;
}

function inferPacketName(handlerType: Type | undefined, handlerToken: InjectionToken): string {
  if (handlerType !== undefined) {
    return handlerType.name.endsWith('Handler')
      ? handlerType.name.slice(0, -'Handler'.length)
      : handlerType.name;
  }
  const tokenName = typeof handlerToken === 'symbol'
    ? handlerToken.description
    : String(handlerToken);
  if (tokenName === undefined || tokenName.trim() === '') {
    throw new framework.ZLinkConfigurationException('ZLink handler packetName is required for anonymous provider tokens.');
  }
  return tokenName;
}

function appendNestHandlerMetadata(handlerToken: InjectionToken, metadata: ZLinkNestHandlerMetadata): void {
  const current = readNestHandlerMetadata(handlerToken);
  nestHandlerMetadataByToken.set(handlerToken, [...current, metadata]);
  if (typeof handlerToken === 'function') {
    Object.defineProperty(handlerToken, ZLINK_NEST_HANDLER_GROUP, {
      configurable: true,
      enumerable: false,
      value: [...current, metadata],
      writable: false
    });
  }
}

function readNestHandlerMetadata(handlerToken: InjectionToken | undefined): readonly ZLinkNestHandlerMetadata[] {
  if (handlerToken === undefined) {
    return [];
  }
  return nestHandlerMetadataByToken.get(handlerToken)
    ?? (typeof handlerToken === 'function'
      ? (((handlerToken as unknown) as Record<symbol, unknown>)[ZLINK_NEST_HANDLER_GROUP] as readonly ZLinkNestHandlerMetadata[] | undefined) ?? []
      : []);
}

@Module({})
export class ZLinkModule {
  static forRoot(options: ZLinkModuleOptions = {}): DynamicModule {
    assertNoLegacyModuleOptions(options);
    if (hasNestHandlerDiscovery(options)) {
      return createDiscoveringZLinkDynamicModule(options);
    }
    return createZLinkDynamicModule(framework.createFrameworkRegistration(createRegistrationOptions(options)));
  }

  static forRootAsync(options: ZLinkModuleAsyncOptions): DynamicModule {
    const registrationProvider: Provider<Promise<ZLinkFrameworkRegistration>> = {
      provide: ZLINK_FRAMEWORK_REGISTRATION,
      inject: [...(options.inject ?? []), DiscoveryService, ModuleRef],
      useFactory: async (...args: unknown[]) => {
        const discovery = args[args.length - 2] as DiscoveryService;
        const moduleRef = args[args.length - 1] as ModuleRef;
        const factoryArgs = args.slice(0, -2);
        const resolvedOptions = assertNoLegacyModuleOptions(await options.useFactory(...factoryArgs));
        return framework.createFrameworkRegistration(createDiscoveredOptions(resolvedOptions, discovery, moduleRef));
      }
    };

    return {
      module: ZLinkModule,
      imports: [...(options.imports ?? []), DiscoveryModule],
      providers: [
        registrationProvider,
        {
          provide: ZLINK_FRAMEWORK_RUNTIME,
          inject: [ZLINK_FRAMEWORK_REGISTRATION, ModuleRef, DiscoveryService],
          useFactory: (registration: ZLinkFrameworkRegistration, moduleRef: ModuleRef, discovery: DiscoveryService) =>
            createRuntimeHost(registration, moduleRef, discovery)
        },
        ...alwaysAvailableClientProviders(),
        ...conditionalClientProvidersForAsync()
      ],
      exports: [
        ZLINK_FRAMEWORK_RUNTIME,
        ...alwaysAvailableClientTokens(),
        ...conditionalClientTokens()
      ]
    };
  }
}

@Module({})
export class ZLinkRegistryModule {
  static forRoot(options: ZLinkRegistryOptions): DynamicModule {
    const runtime = new framework.ZLinkRegistryRuntime({ registration: options });
    const query = new framework.DefaultZLinkRegistryQuery(runtime);
    const providers: Provider[] = [
      { provide: ZLINK_REGISTRY_RUNTIME, useValue: runtime },
      { provide: ZLINK_REGISTRY_QUERY, useValue: query }
    ];
    return {
      module: ZLinkRegistryModule,
      providers,
      exports: providers.map(providerToken)
    };
  }

  static forRootAsync(options: ZLinkRegistryModuleAsyncOptions): DynamicModule {
    return createAsyncRegistryDynamicModule({
      module: ZLinkRegistryModule,
      options,
      runtimeToken: ZLINK_REGISTRY_RUNTIME,
      queryToken: ZLINK_REGISTRY_QUERY,
      createRuntime: (registration: ZLinkRegistryOptions) =>
        new framework.ZLinkRegistryRuntime({ registration }),
      createQuery: (runtime: InstanceType<FrameworkModule['ZLinkRegistryRuntime']>) =>
        new framework.DefaultZLinkRegistryQuery(runtime)
    });
  }
}

@Module({})
export class ZLinkRegistryQueryClientModule {
  static forRoot(options: ZLinkRegistryQueryClientOptions): DynamicModule {
    return {
      module: ZLinkRegistryQueryClientModule,
      providers: [{
        provide: ZLINK_REGISTRY_QUERY_CLIENT,
        useFactory: () => new framework.DefaultZLinkRegistryQueryClient({ registration: options })
      }],
      exports: [ZLINK_REGISTRY_QUERY_CLIENT]
    };
  }

  static forRootAsync(options: ZLinkRegistryQueryClientModuleAsyncOptions): DynamicModule {
    return {
      module: ZLinkRegistryQueryClientModule,
      imports: options.imports,
      providers: [{
        provide: ZLINK_REGISTRY_QUERY_CLIENT,
        inject: options.inject === undefined ? undefined : [...options.inject],
        useFactory: async (...args: unknown[]) =>
          new framework.DefaultZLinkRegistryQueryClient({ registration: await options.useFactory(...args) })
      }],
      exports: [ZLINK_REGISTRY_QUERY_CLIENT]
    };
  }
}

export function createZLinkDynamicModule(registration: ZLinkFrameworkRegistration): DynamicModule {
  const providers: Provider[] = [
    { provide: ZLINK_FRAMEWORK_REGISTRATION, useValue: registration },
    {
      provide: ZLINK_FRAMEWORK_RUNTIME,
      inject: [ModuleRef, DiscoveryService],
      useFactory: (moduleRef: ModuleRef, discovery: DiscoveryService) => createRuntimeHost(registration, moduleRef, discovery)
    },
    ...alwaysAvailableClientProviders(registration),
    ...conditionalClientProviders(registration)
  ];

  return {
    module: ZLinkModule,
    imports: [DiscoveryModule],
    providers,
    exports: providers.map(providerToken)
  };
}

function createAsyncRegistryDynamicModule(options: {
  readonly module: Type;
  readonly options: ZLinkRegistryModuleAsyncOptions;
  readonly runtimeToken: InjectionToken;
  readonly queryToken: InjectionToken;
  readonly createRuntime: (registration: ZLinkRegistryOptions) => InstanceType<FrameworkModule['ZLinkRegistryRuntime']>;
  readonly createQuery: (
    runtime: InstanceType<FrameworkModule['ZLinkRegistryRuntime']>
  ) => InstanceType<FrameworkModule['DefaultZLinkRegistryQuery']>;
}): DynamicModule {
  return {
    module: options.module,
    imports: options.options.imports,
    providers: [
      {
        provide: options.runtimeToken,
        inject: options.options.inject === undefined ? undefined : [...options.options.inject],
        useFactory: async (...args: unknown[]) => options.createRuntime(await options.options.useFactory(...args))
      },
      {
        provide: options.queryToken,
        inject: [options.runtimeToken],
        useFactory: options.createQuery
      }
    ],
    exports: [options.runtimeToken, options.queryToken]
  };
}

function createDiscoveringZLinkDynamicModule(options: ZLinkModuleOptions): DynamicModule {
  const registrationProvider: Provider = {
    provide: ZLINK_FRAMEWORK_REGISTRATION,
    inject: [DiscoveryService, ModuleRef],
    useFactory: (discovery: DiscoveryService, moduleRef: ModuleRef) =>
      framework.createFrameworkRegistration(createDiscoveredOptions(options, discovery, moduleRef))
  };

  return {
    module: ZLinkModule,
    imports: [DiscoveryModule],
    providers: [
      registrationProvider,
      {
        provide: ZLINK_FRAMEWORK_RUNTIME,
        inject: [ZLINK_FRAMEWORK_REGISTRATION, ModuleRef, DiscoveryService],
        useFactory: (registration: ZLinkFrameworkRegistration, moduleRef: ModuleRef, discovery: DiscoveryService) =>
          createRuntimeHost(registration, moduleRef, discovery)
      },
      ...alwaysAvailableClientProviders(),
      ...conditionalClientProvidersForAsync()
    ],
    exports: [
      ZLINK_FRAMEWORK_RUNTIME,
      ...alwaysAvailableClientTokens(),
      ...conditionalClientTokens()
    ]
  };
}

function createDiscoveredOptions(
  options: ZLinkModuleOptions,
  discovery: DiscoveryService,
  moduleRef: ModuleRef
): ZLinkFrameworkRegistrationOptions {
  const registrationOptions = createRegistrationOptions(options);
  const channels: Record<string, ZLinkChannelOptions> = { ...(registrationOptions.channels ?? {}) };
  const routerMeshes = new Map<string, ZLinkRouteChannelOptions>();
  const providerRefs = discoverProviderRefs(discovery, moduleRef);

  for (const [channelName, channel] of Object.entries(options.clientServerChannels ?? {})) {
    const requestHandlers = createDiscoveredRequestHandlers(
      providerRefs,
      channel.handlerGroups,
      moduleRef
    );
    channels[channelName] = {
      ...channels[channelName],
      requestHandlers: channel.server === undefined
        ? channels[channelName]?.requestHandlers
        : [
            ...(channels[channelName]?.requestHandlers ?? []),
            ...requestHandlers
          ]
    };
  }

  for (const [channelName, channel] of Object.entries(options.fanoutChannels ?? {})) {
    const publishHandlers = createDiscoveredPublishHandlers(providerRefs, channel.handlerGroups, moduleRef);
    channels[channelName] = {
      ...channels[channelName],
      publishHandlers: [
        ...(channels[channelName]?.publishHandlers ?? []),
        ...publishHandlers
      ]
    };
  }

  for (const routeChannel of registrationOptions.routeChannels ?? []) {
    const normalized = typeof routeChannel === 'string'
      ? { routerChannelId: routeChannel }
      : { ...routeChannel };
    routerMeshes.set(normalized.routerChannelId, normalized);
  }
  for (const [routerMeshName, routerMesh] of Object.entries(options.routerMeshes ?? {})) {
    const existing = routerMeshes.get(routerMeshName) ?? { routerChannelId: routerMeshName };
    const requestHandlers = createDiscoveredRequestHandlers(
      providerRefs,
      routerMesh.handlerGroups,
      moduleRef
    );
    const sendHandlers = createDiscoveredSendHandlers(
      providerRefs,
      routerMesh.handlerGroups,
      moduleRef
    );
    routerMeshes.set(routerMeshName, {
      ...existing,
      requestHandlers: [
        ...(existing.requestHandlers ?? []),
        ...requestHandlers
      ],
      sendHandlers: [
        ...(existing.sendHandlers ?? []),
        ...sendHandlers
      ]
    });
  }

  return {
    ...registrationOptions,
    channels,
    routeChannels: [...routerMeshes.values()]
  };
}

function createRegistrationOptions(options: ZLinkModuleOptions): ZLinkFrameworkRegistrationOptions {
  const channels: Record<string, ZLinkChannelOptions> = {};
  const routeChannels: ZLinkRouteChannelOptions[] = [];

  for (const [name, channel] of Object.entries(options.clientServerChannels ?? {})) {
    assertChannelNameAvailable(channels, name, 'ClientServerChannel');
    channels[name] = {
      client: channel.client,
      requestHandlers: channel.requestHandlers,
      server: channel.server
    };
  }

  for (const [name, channel] of Object.entries(options.fanoutChannels ?? {})) {
    assertChannelNameAvailable(channels, name, 'FanoutChannel');
    channels[name] = {
      publishHandlers: channel.publishHandlers,
      publisher: channel.publisher,
      subscriber: channel.subscriber
    };
  }

  for (const [name, channel] of Object.entries(options.dealerMeshChannels ?? {})) {
    assertChannelNameAvailable(channels, name, 'DealerMeshChannel');
    channels[name] = {
      dealerMesh: { ...channel }
    };
  }

  for (const [name, routerMesh] of Object.entries(options.routerMeshes ?? {})) {
    const { handlerGroups: _handlerGroups, ...routeChannel } = routerMesh;
    routeChannels.push({
      routerChannelId: name,
      ...routeChannel
    });
  }

  return {
    actorFactories: options.actorFactories,
    channels,
    discovery: options.discovery,
    registrySpotRemoteAddresses: options.registrySpotRemoteAddresses,
    routeChannels,
    spotFactories: options.spotFactories,
    spotNodes: options.spotNodes,
    spotPublisherClients: options.spotPublisherClients,
    spotRemoteAddressResolver: options.spotRemoteAddressResolver,
    streamNodes: options.streams
  };
}

function assertChannelNameAvailable(
  channels: Readonly<Record<string, ZLinkChannelOptions>>,
  name: string,
  kind: string
): void {
  if (channels[name] !== undefined) {
    throw new framework.ZLinkConfigurationException(`Channel '${name}' is already registered before ${kind}.`);
  }
}

function assertNoLegacyModuleOptions(options: ZLinkModuleOptions): ZLinkModuleOptions {
  const legacy = options as ZLinkModuleOptions & {
    readonly channels?: unknown;
    readonly routeChannels?: unknown;
    readonly streamNodes?: unknown;
  };
  if (legacy.channels !== undefined) {
    throw new framework.ZLinkConfigurationException(
      'NestJS ZLinkModule uses clientServerChannels, fanoutChannels, dealerMeshChannels, and routerMeshes instead of channels.'
    );
  }
  if (legacy.routeChannels !== undefined) {
    throw new framework.ZLinkConfigurationException('NestJS ZLinkModule uses routerMeshes instead of routeChannels.');
  }
  if (legacy.streamNodes !== undefined) {
    throw new framework.ZLinkConfigurationException('NestJS ZLinkModule uses streams instead of streamNodes.');
  }
  return options;
}

interface DiscoveredNestProvider {
  readonly handlerKey: InjectionToken;
  readonly handlerName: string;
  readonly token: InjectionToken;
  readonly instance?: Record<string, unknown>;
  readonly metadata: ZLinkNestHandlerMetadata;
}

function createDiscoveredRequestHandlers(
  providerRefs: readonly DiscoveredNestProvider[],
  handlerGroups: readonly string[] | undefined,
  moduleRef: ModuleRef
): NonNullable<ZLinkChannelOptions['requestHandlers']> {
  return createDiscoveredHandlerRegistrations(providerRefs, handlerGroups, 'request', (ref, metadata) => ({
    async handle(payload: Buffer, context: ZLinkRequestContext) {
      return await invokeDiscoveredHandler(moduleRef, ref, metadata, payload, context);
    }
  }));
}

function createDiscoveredSendHandlers(
  providerRefs: readonly DiscoveredNestProvider[],
  handlerGroups: readonly string[] | undefined,
  moduleRef: ModuleRef
): NonNullable<NonNullable<ZLinkChannelOptions['routeMesh']>['sendHandlers']> {
  return createDiscoveredHandlerRegistrations(providerRefs, handlerGroups, 'send', (ref, metadata) => ({
    async handle(payload: Buffer, context: ZLinkRouteSendContext) {
      await invokeDiscoveredHandler(moduleRef, ref, metadata, payload, context);
    }
  }));
}

function createDiscoveredPublishHandlers(
  providerRefs: readonly DiscoveredNestProvider[],
  handlerGroups: readonly string[] | undefined,
  moduleRef: ModuleRef
): NonNullable<ZLinkChannelOptions['publishHandlers']> {
  return createDiscoveredHandlerRegistrations(providerRefs, handlerGroups, 'publish', (ref, metadata) => ({
    async handle(payload: Buffer, context: ZLinkPublishContext) {
      await invokeDiscoveredHandler(moduleRef, ref, metadata, payload, context);
    }
  }));
}

function createDiscoveredHandlerRegistrations<THandler>(
  providerRefs: readonly DiscoveredNestProvider[],
  handlerGroups: readonly string[] | undefined,
  kind: string,
  createHandler: (ref: DiscoveredNestProvider, metadata: ZLinkNestHandlerMetadata) => THandler
): Array<{ readonly packetName: string; readonly handler: THandler }> {
  const descriptors = createDiscoveredHandlerDescriptors(providerRefs, handlerGroups, kind);

  return descriptors.map(({ ref, metadata }) => ({
    packetName: metadata.packetName,
    handler: createHandler(ref, metadata)
  }));
}

function createDiscoveredHandlerDescriptors(
  providerRefs: readonly DiscoveredNestProvider[],
  handlerGroups: readonly string[] | undefined,
  kind: string
): Array<{ readonly ref: DiscoveredNestProvider; readonly metadata: ZLinkNestHandlerMetadata }> {
  if ((handlerGroups ?? []).length === 0) {
    return [];
  }

  const groups = new Set(handlerGroups);
  const seen = new Map<string, InjectionToken>();
  const selected: Array<{ readonly ref: DiscoveredNestProvider; readonly metadata: ZLinkNestHandlerMetadata }> = [];
  for (const ref of providerRefs) {
    const metadata = ref.metadata;
    if (metadata.kind !== kind || !groups.has(metadata.groupName)) {
      continue;
    }
    const key = `${metadata.kind}:${metadata.packetName}`;
    const previousType = seen.get(key);
    if (previousType === ref.handlerKey) {
      continue;
    }
    if (previousType !== undefined) {
      throw new framework.ZLinkConfigurationException(
        `Duplicate handler '${metadata.groupName}:${metadata.kind}:${metadata.packetName}'.`
      );
    }
    seen.set(key, ref.handlerKey);
    selected.push({ ref, metadata });
  }
  return selected;
}

function discoverProviderRefs(discovery: DiscoveryService, moduleRef: ModuleRef): DiscoveredNestProvider[] {
  const refs: DiscoveredNestProvider[] = [];
  const seen = new Set<string>();

  for (const wrapper of discovery.getProviders()) {
    const token = wrapper.token as InjectionToken | undefined;
    if (token === undefined) {
      continue;
    }

    const candidates = [wrapper.metatype, wrapper.instance?.constructor, token]
      .filter((value): value is InjectionToken =>
        typeof value === 'function' || typeof value === 'string' || typeof value === 'symbol'
      );
    for (const handlerKey of new Set(candidates)) {
      for (const metadata of readNestHandlerMetadata(handlerKey)) {
        const handlerName = handlerKeyName(handlerKey);
        const key = `${String(token)}:${handlerName}:${metadata.groupName}:${metadata.kind}:${metadata.packetName}`;
        if (seen.has(key)) {
          continue;
        }
        seen.add(key);
        refs.push({
          handlerKey,
          handlerName,
          token,
          instance: wrapper.instance === undefined ? undefined : wrapper.instance as Record<string, unknown>,
          metadata
        });
      }
    }
  }

  for (const [handlerKey, metadataList] of nestHandlerMetadataByToken) {
    if (!isInjectionToken(handlerKey)) {
      continue;
    }
    const instance = tryGetProviderInstance(moduleRef, handlerKey);
    if (instance === undefined) {
      continue;
    }
    const handlerName = handlerKeyName(handlerKey);
    for (const metadata of metadataList) {
      const key = `${String(handlerKey)}:${handlerName}:${metadata.groupName}:${metadata.kind}:${metadata.packetName}`;
      if (seen.has(key)) {
        continue;
      }
      seen.add(key);
      refs.push({
        handlerKey,
        handlerName,
        token: handlerKey,
        instance,
        metadata
      });
    }
  }

  return refs;
}

function isInjectionToken(value: unknown): value is InjectionToken {
  return typeof value === 'function' || typeof value === 'string' || typeof value === 'symbol';
}

function tryGetProviderInstance(moduleRef: ModuleRef, token: InjectionToken): Record<string, unknown> | undefined {
  try {
    return moduleRef.get(token, { strict: false }) as Record<string, unknown>;
  } catch {
    return undefined;
  }
}

async function invokeDiscoveredHandler(
  moduleRef: ModuleRef,
  ref: DiscoveredNestProvider,
  metadata: ZLinkNestHandlerMetadata,
  payload: Buffer,
  context: ZLinkRequestContext | ZLinkRouteSendContext | ZLinkPublishContext
): Promise<unknown> {
  const instance = moduleRef.get(ref.token, { strict: false }) as Record<string, unknown>;
  const methodName = metadata.methodName ?? 'handle';
  const method = instance[methodName];
  if (typeof method !== 'function') {
    throw new framework.ZLinkConfigurationException(
      `Discovered handler ${ref.handlerName}.${methodName} is not callable.`
    );
  }
  return await method.call(instance, decodePayload(payload), context);
}

function handlerKeyName(handlerKey: InjectionToken): string {
  if (typeof handlerKey === 'function') {
    return handlerKey.name;
  }
  if (typeof handlerKey === 'symbol') {
    return handlerKey.description ?? handlerKey.toString();
  }
  return handlerKey;
}

function decodePayload(payload: Buffer | Uint8Array | string | unknown): unknown {
  if (Buffer.isBuffer(payload) || payload instanceof Uint8Array) {
    return JSON.parse(Buffer.from(payload).toString());
  }
  if (typeof payload === 'string') {
    return JSON.parse(payload);
  }
  return payload;
}

function hasNestHandlerDiscovery(options: ZLinkModuleOptions): boolean {
  return [
    ...Object.values(options.clientServerChannels ?? {}),
    ...Object.values(options.fanoutChannels ?? {}),
    ...Object.values(options.routerMeshes ?? {})
  ].some(
    (channel) => (channel.handlerGroups ?? []).length > 0
  );
}

type FrameworkRuntimeHost = InstanceType<FrameworkModule['ZLinkFrameworkRuntimeHost']>;

interface AlwaysAvailableClientProviderSpec {
  readonly token: InjectionToken;
  create(registration: ZLinkFrameworkRegistration, runtime: FrameworkRuntimeHost): unknown;
}

const ALWAYS_AVAILABLE_CLIENT_PROVIDER_SPECS: readonly AlwaysAvailableClientProviderSpec[] = [
  {
    token: ZLINK_CHANNEL_CLIENT,
    create: (registration, runtime) => new framework.DefaultZLinkChannelClient(registration, runtime.channelTransport)
  },
  {
    token: ZLINK_FANOUT_CLIENT,
    create: (registration, runtime) => new framework.DefaultZLinkFanoutClient(registration, runtime.channelTransport)
  },
  {
    token: ZLINK_ROUTE_CLIENT,
    create: (registration, runtime) => new framework.DefaultZLinkRouteClient(registration, runtime.routeTransport)
  },
  {
    token: ZLINK_BOUND_SESSION_FACTORY,
    create: (_registration, runtime) => runtime.boundSessionFactory
  }
];

function alwaysAvailableClientProviders(registration?: ZLinkFrameworkRegistration): Provider[] {
  return [
    ...ALWAYS_AVAILABLE_CLIENT_PROVIDER_SPECS.map((spec) =>
      createAlwaysAvailableClientProvider(spec, registration)
    ),
    { provide: ZLINK_MESSAGE_METADATA_POLICY, useValue: Object.freeze({ forward: true }) }
  ];
}

function createAlwaysAvailableClientProvider(
  spec: AlwaysAvailableClientProviderSpec,
  registration: ZLinkFrameworkRegistration | undefined
): Provider {
  if (registration !== undefined) {
    return {
      provide: spec.token,
      inject: [ZLINK_FRAMEWORK_RUNTIME],
      useFactory: (runtime: FrameworkRuntimeHost) => spec.create(registration, runtime)
    };
  }
  return {
    provide: spec.token,
    inject: [ZLINK_FRAMEWORK_REGISTRATION, ZLINK_FRAMEWORK_RUNTIME],
    useFactory: (resolved: ZLinkFrameworkRegistration, runtime: FrameworkRuntimeHost) =>
      spec.create(resolved, runtime)
  };
}

function alwaysAvailableClientTokens(): InjectionToken[] {
  return [
    ZLINK_CHANNEL_CLIENT,
    ZLINK_ROUTE_CLIENT,
    ZLINK_FANOUT_CLIENT,
    ZLINK_BOUND_SESSION_FACTORY,
    ZLINK_MESSAGE_METADATA_POLICY
  ];
}

function conditionalClientProviders(registration: ZLinkFrameworkRegistration): Provider[] {
  const providers = CONDITIONAL_CLIENT_PROVIDER_SPECS
    .filter((spec) => spec.isEnabled(registration))
    .map((spec) => createConditionalClientProvider(spec, registration));
  if (framework.hasSpotRemoteAddressResolver(registration)) {
    providers.push(...spotRemoteAddressResolverProviders(registration));
  }

  return providers;
}

interface ConditionalClientProviderSpec {
  readonly token: InjectionToken;
  readonly requiresRuntime: boolean;
  isEnabled(registration: ZLinkFrameworkRegistration): boolean;
  create(
    registration: ZLinkFrameworkRegistration,
    runtime: FrameworkRuntimeHost | undefined,
    moduleRef: ModuleRef | undefined,
    discovery: DiscoveryService | undefined
  ): unknown | Promise<unknown>;
}

const CONDITIONAL_CLIENT_PROVIDER_SPECS: readonly ConditionalClientProviderSpec[] = [
  {
    token: ZLINK_SPOT_MANAGER,
    requiresRuntime: false,
    isEnabled: (registration) => framework.hasSpotNode(registration),
    create: (registration, _runtime, moduleRef, discovery) => createSpotManager(registration, moduleRef, discovery)
  },
  {
    token: ZLINK_SPOT_OUTBOUND,
    requiresRuntime: true,
    isEnabled: (registration) => framework.hasSpotNode(registration),
    create: (registration, runtime, moduleRef, discovery) =>
      createSpotOutbound(registration, requireRuntime(runtime), moduleRef, discovery)
  },
  {
    token: ZLINK_SPOT_PUBLISHER_CLIENT,
    requiresRuntime: true,
    isEnabled: (registration) => framework.hasSpotPublisherClient(registration),
    create: (registration, runtime) =>
      new framework.DefaultZLinkSpotPublisherClient(registration, requireRuntime(runtime).spotPublisherTransport)
  },
  {
    token: ZLINK_ACTOR_MANAGER,
    requiresRuntime: false,
    isEnabled: (registration) => framework.hasActorManager(registration),
    create: (registration, _runtime, moduleRef, discovery) => new framework.DefaultZLinkActorManager({
      actorFactories: registration.actorFactories,
      providerResolver: moduleRef === undefined ? undefined : createProviderResolver(moduleRef, discovery)
    })
  }
];

function conditionalClientProvidersForAsync(): Provider[] {
  return [
    ...CONDITIONAL_CLIENT_PROVIDER_SPECS.map(createConditionalClientProviderForAsync),
    {
      provide: ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER,
      inject: [ZLINK_FRAMEWORK_REGISTRATION, ModuleRef, DiscoveryService],
      useFactory: async (registration: ZLinkFrameworkRegistration, moduleRef: ModuleRef, discovery: DiscoveryService) => {
        if (!framework.hasSpotRemoteAddressResolver(registration)) {
          return null;
        }
        return await createSpotRemoteAddressResolver(registration, moduleRef, discovery);
      }
    }
  ];
}

function createConditionalClientProviderForAsync(spec: ConditionalClientProviderSpec): Provider {
  return {
    provide: spec.token,
    inject: spec.requiresRuntime
      ? [ZLINK_FRAMEWORK_REGISTRATION, ZLINK_FRAMEWORK_RUNTIME, ModuleRef, DiscoveryService]
      : [ZLINK_FRAMEWORK_REGISTRATION, ModuleRef, DiscoveryService],
    useFactory: (
      registration: ZLinkFrameworkRegistration,
      runtimeOrModuleRef?: FrameworkRuntimeHost | ModuleRef,
      moduleRefOrDiscovery?: ModuleRef | DiscoveryService,
      maybeDiscovery?: DiscoveryService
    ) => {
      if (!spec.isEnabled(registration)) {
        return null;
      }
      const runtime = spec.requiresRuntime ? runtimeOrModuleRef as FrameworkRuntimeHost : undefined;
      const moduleRef = spec.requiresRuntime ? moduleRefOrDiscovery as ModuleRef : runtimeOrModuleRef as ModuleRef;
      const discovery = spec.requiresRuntime ? maybeDiscovery : moduleRefOrDiscovery as DiscoveryService;
      return spec.create(registration, runtime, moduleRef, discovery);
    }
  };
}

function createConditionalClientProvider(
  spec: ConditionalClientProviderSpec,
  registration: ZLinkFrameworkRegistration
): Provider {
  return {
    provide: spec.token,
    inject: spec.requiresRuntime ? [ZLINK_FRAMEWORK_RUNTIME, ModuleRef, DiscoveryService] : [ModuleRef, DiscoveryService],
    useFactory: (
      runtimeOrModuleRef: FrameworkRuntimeHost | ModuleRef,
      moduleRefOrDiscovery?: ModuleRef | DiscoveryService,
      maybeDiscovery?: DiscoveryService
    ) => {
      const runtime = spec.requiresRuntime ? runtimeOrModuleRef as FrameworkRuntimeHost : undefined;
      const moduleRef = spec.requiresRuntime ? moduleRefOrDiscovery as ModuleRef : runtimeOrModuleRef as ModuleRef;
      const discovery = spec.requiresRuntime ? maybeDiscovery : moduleRefOrDiscovery as DiscoveryService;
      return spec.create(registration, runtime, moduleRef, discovery);
    }
  };
}

function requireRuntime(runtime: FrameworkRuntimeHost | undefined): FrameworkRuntimeHost {
  if (runtime === undefined) {
    throw new framework.ZLinkConfigurationException('ZLink runtime host is not available.');
  }
  return runtime;
}

function conditionalClientTokens(): InjectionToken[] {
  return [
    ZLINK_SPOT_MANAGER,
    ZLINK_SPOT_OUTBOUND,
    ZLINK_SPOT_PUBLISHER_CLIENT,
    ZLINK_ACTOR_MANAGER,
    ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER
  ];
}

function createRuntimeHost(
  registration: ZLinkFrameworkRegistration,
  moduleRef: ModuleRef,
  discovery: DiscoveryService
): RuntimeHostWithNestLifecycle {
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration,
    providerResolver: createProviderResolver(moduleRef, discovery)
  }) as RuntimeHostWithNestLifecycle;
  runtime.onModuleInit = async () => {
    await runtime.start();
  };
  runtime.onModuleDestroy = async () => {
    await runtime.stop();
  };
  return runtime;
}

function createProviderResolver(moduleRef: ModuleRef, discovery?: DiscoveryService): ZLinkProviderResolver {
  return {
    get<T>(type: Type<T>): T | undefined {
      const discovered = findDiscoveredProviderInstance<T>(discovery, type);
      if (discovered !== undefined) {
        return discovered;
      }
      try {
        return moduleRef.get(type, { strict: false });
      } catch {
        return undefined;
      }
    },
    async create<T>(type: Type<T>): Promise<T> {
      const existing = this.get?.(type);
      if (existing !== undefined) {
        return existing;
      }
      return moduleRef.create(type as unknown as import('@nestjs/common').Type<T>);
    }
  };
}

function findDiscoveredProviderInstance<T>(discovery: DiscoveryService | undefined, type: Type<T>): T | undefined {
  for (const wrapper of discovery?.getProviders() ?? []) {
    if (
      wrapper.instance !== undefined
      && wrapper.instance !== null
      && (
        wrapper.token === type
        || wrapper.metatype === type
        || wrapper.instance.constructor === type
      )
    ) {
      return wrapper.instance as T;
    }
  }
  return undefined;
}

function providerToken(provider: Provider): InjectionToken {
  return typeof provider === 'function' ? provider : provider.provide;
}

function createSpotManager(
  registration: ZLinkFrameworkRegistration,
  moduleRef: ModuleRef | undefined,
  discovery: DiscoveryService | undefined
): InstanceType<FrameworkModule['DefaultZLinkSpotManager']> {
  return new framework.DefaultZLinkSpotManager({
    spotFactories: [...registration.spotFactories],
    providerResolver: moduleRef === undefined ? undefined : createProviderResolver(moduleRef, discovery)
  });
}

async function createSpotOutbound(
  registration: ZLinkFrameworkRegistration,
  runtime: InstanceType<FrameworkModule['ZLinkFrameworkRuntimeHost']>,
  moduleRef: ModuleRef | undefined,
  discovery: DiscoveryService | undefined
): Promise<InstanceType<FrameworkModule['DefaultZLinkSpotOutbound']>> {
  const resolver = framework.hasSpotRemoteAddressResolver(registration)
    ? await createSpotRemoteAddressResolver(registration, moduleRef, discovery)
    : undefined;
  return new framework.DefaultZLinkSpotOutbound(
    new framework.ZLinkSpotSerialExecutor(),
    undefined,
    undefined,
    resolver,
    runtime.routeTransport
  );
}

async function createSpotRemoteAddressResolver(
  registration: ZLinkFrameworkRegistration,
  moduleRef?: ModuleRef,
  discovery?: DiscoveryService
): Promise<ZLinkSpotRemoteAddressResolver> {
  if (registration.spotRemoteAddressResolverType !== undefined) {
    const providerResolver = moduleRef === undefined ? undefined : createProviderResolver(moduleRef, discovery);
    const resolverType = registration.spotRemoteAddressResolverType as Type<ZLinkSpotRemoteAddressResolver>;
    const resolver = await providerResolver?.create?.(resolverType);
    if (resolver === undefined) {
      throw new framework.ZLinkConfigurationException('Spot remote address resolver provider is not available.');
    }
    return resolver;
  }
  if (registration.registrySpotRemoteAddresses !== undefined) {
    return new framework.ZLinkRegistrySpotRemoteAddressResolver({ registration });
  }
  throw new framework.ZLinkConfigurationException('Spot remote address resolver is not registered.');
}

function spotRemoteAddressResolverProviders(registration: ZLinkFrameworkRegistration): Provider[] {
  const resolverType = registration.spotRemoteAddressResolverType;
  if (resolverType !== undefined) {
    return [
      { provide: resolverType, useClass: resolverType },
      {
        provide: ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER,
        inject: [resolverType],
        useFactory: (resolver: ZLinkSpotRemoteAddressResolver) => resolver
      }
    ];
  }
  return [{
    provide: ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER,
    inject: [ModuleRef, DiscoveryService],
    useFactory: (moduleRef: ModuleRef, discovery: DiscoveryService) =>
      createSpotRemoteAddressResolver(registration, moduleRef, discovery)
  }];
}

function loadFramework(): FrameworkModule {
  const requireFramework = createRequire(__filename);
  try {
    return requireFramework('@zlink-systems/framework') as FrameworkModule;
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== 'MODULE_NOT_FOUND') {
      throw error;
    }
    return requireFramework(path.resolve(__dirname, '../../framework/dist')) as FrameworkModule;
  }
}
