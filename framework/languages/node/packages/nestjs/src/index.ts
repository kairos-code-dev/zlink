import 'reflect-metadata';
import { createRequire } from 'node:module';
import fs from 'node:fs';
import path from 'node:path';
import { Injectable, Module } from '@nestjs/common';
import type { DynamicModule, InjectionToken, ModuleMetadata, OnModuleDestroy, OnModuleInit, Provider } from '@nestjs/common';
import { DiscoveryModule, DiscoveryService, ModuleRef } from '@nestjs/core';
import type {
  Type,
  ZLinkActor,
  ZLinkChannelPublishHandlerRegistration,
  ZLinkChannelRequestHandlerRegistration,
  ZLinkChannelSendHandlerRegistration,
  ZLinkClientCapabilityOptions,
  ZLinkDealerMeshChannelOptions,
  ZLinkChannelOptions,
  ZLinkCodecExtension,
  ZLinkCodecRegistryOptions,
  ZLinkDispatchOptionsBuilder,
  ZLinkFrameworkRegistration,
  ZLinkFrameworkRegistrationOptions,
  ZLinkMessageDispatchErrorObserver,
  ZLinkMessageSerializer,
  ZLinkNamedCodec,
  ZLinkProviderResolver,
  ZLinkPublisherCapabilityOptions,
  ZLinkPublishContext,
  ZLinkRequestContext,
  ZLinkSendContext,
  ZLinkRouteChannelOptions,
  ZLinkRouteChannelRequestHandlerRegistration,
  ZLinkRouteChannelSendHandlerRegistration,
  ZLinkRouteRequestContext,
  ZLinkRouteSendContext,
  ZLinkEntrySpot,
  ZLinkEntrySpotOptions,
  ZLinkEntrySpotActorSendHandlerRegistration,
  ZLinkEntrySpotActorRequestHandlerRegistration,
  ZLinkSpot,
  ZLinkSpotActorSendHandlerRegistration,
  ZLinkSpotActorRequestHandlerRegistration,
  ZLinkRegistryOptions,
  ZLinkRegistryQueryClientOptions,
  ZLinkSpotNodeRegistrationOptions,
  ZLinkSpotNodeOptions,
  ZLinkSpotRemoteAddressResolver,
  ZLinkSession,
  ZLinkSessionFactory,
  ZLinkTimerOptions,
  ZLinkStreamNodeOptions
} from '@zlink-systems/framework';

type RuntimeConstructor<T> = new (...args: unknown[]) => T;
type MutableCodecRegistryOptions = {
  codecs: ZLinkNamedCodec[];
  serializers: NonNullable<ZLinkCodecRegistryOptions['serializers']>[number][];
  streamCodecs: NonNullable<ZLinkCodecRegistryOptions['streamCodecs']>[number][];
};

interface FrameworkRuntimeHost {
  readonly channelTransport: unknown;
  readonly routeTransport: unknown;
  readonly spotPublisherTransport: unknown;
  readonly streamBindingRuntime: unknown;
  readonly boundSessionFactory: {
    create(actorId: string): unknown;
  };
  readonly isStarted: boolean;
  start(): Promise<void>;
  stop(): Promise<void>;
  onApplicationBootstrap(): Promise<void>;
  onApplicationShutdown(): Promise<void>;
  setActorManager?(actorManager: unknown): void;
  setSpotManager?(spotManager: unknown): void;
  createActorManagerOptions?(remoteAddressResolver?: ZLinkSpotRemoteAddressResolver): object;
  createSpotManagerOptions?(): object;
  createRegistrySpotRemoteAddressResolver?(): ZLinkSpotRemoteAddressResolver;
}

interface RegistryRuntime {
  readonly isStarted: boolean;
  start(signal?: AbortSignal): Promise<void>;
  stop(signal?: AbortSignal): Promise<void>;
}

interface FrameworkModule {
  readonly ZLinkConfigurationException: new (message: string) => Error;
  readonly ZLinkRegistryRuntime: RuntimeConstructor<RegistryRuntime>;
  readonly DefaultZLinkRegistryQuery: new (runtime: RegistryRuntime) => unknown;
  readonly DefaultZLinkRegistryQueryClient: new (options: { readonly registration: ZLinkRegistryQueryClientOptions }) => unknown;
  readonly ZLinkFrameworkRuntimeHost: new (options: {
    readonly registration: ZLinkFrameworkRegistration;
    readonly providerResolver?: ZLinkProviderResolver;
  }) => FrameworkRuntimeHost;
  readonly DefaultZLinkChannelClient: new (registration: ZLinkFrameworkRegistration, transport: unknown) => unknown;
  readonly DefaultZLinkFanoutClient: new (registration: ZLinkFrameworkRegistration, transport: unknown) => unknown;
  readonly DefaultZLinkRouteClient: new (registration: ZLinkFrameworkRegistration, transport: unknown) => unknown;
  readonly DefaultZLinkSpotPublisherClient: new (registration: ZLinkFrameworkRegistration, transport: unknown) => unknown;
  readonly DefaultZLinkActorManager: new (options: Record<string, unknown>) => unknown;
  readonly DefaultZLinkSpotManager: new (options: Record<string, unknown>) => unknown;
  readonly DefaultZLinkSpotOutbound: new (...args: unknown[]) => unknown;
  readonly ZLinkSpotSerialExecutor: new () => unknown;
  readonly ZLinkSpotWorkerRuntime: new (options?: unknown) => unknown;
  readonly ZLinkRegistrySpotRemoteAddressResolver: new (options: { readonly registration: ZLinkFrameworkRegistration }) => ZLinkSpotRemoteAddressResolver;
  createFrameworkRegistration(options: ZLinkFrameworkRegistrationOptions): ZLinkFrameworkRegistration;
  hasSpotNode(registration: ZLinkFrameworkRegistration): boolean;
  hasActorManager(registration: ZLinkFrameworkRegistration): boolean;
  hasSpotPublisherClient(registration: ZLinkFrameworkRegistration): boolean;
  hasSpotRemoteAddressResolver(registration: ZLinkFrameworkRegistration): boolean;
}

const framework = loadFramework();

type RuntimeHost = FrameworkRuntimeHost;
type RuntimeHostWithNestLifecycle = RuntimeHost & OnModuleInit & OnModuleDestroy;

export interface ZLinkModuleFactoryOptions {
  readonly useFactory: (...args: unknown[]) => ZLinkModuleOptions | Promise<ZLinkModuleOptions>;
  readonly inject?: readonly InjectionToken[];
  readonly imports?: ModuleMetadata['imports'];
}

export interface ZLinkRegistryModuleFactoryOptions {
  readonly useFactory: (...args: unknown[]) => ZLinkRegistryOptions | Promise<ZLinkRegistryOptions>;
  readonly inject?: readonly InjectionToken[];
  readonly imports?: ModuleMetadata['imports'];
}

export interface ZLinkRegistryQueryClientModuleFactoryOptions {
  readonly useFactory: (...args: unknown[]) => ZLinkRegistryQueryClientOptions | Promise<ZLinkRegistryQueryClientOptions>;
  readonly inject?: readonly InjectionToken[];
  readonly imports?: ModuleMetadata['imports'];
}

interface ZLinkNestHandlerDiscoveryOptions {
  readonly handlerGroups?: readonly string[];
}

interface ZLinkNestManualHandlerOptions {
  readonly packetName: string;
  readonly handlerType: Type;
}

type Mutable<T> = {
  -readonly [K in keyof T]: T[K];
};

interface ZLinkNestClientServerChannelOptions extends ZLinkNestHandlerDiscoveryOptions {
  readonly server?: { readonly bind?: string };
  readonly client?: ZLinkClientCapabilityOptions;
  readonly requestHandlers?: readonly ZLinkChannelRequestHandlerRegistration[];
  readonly requestHandlerTypes?: readonly ZLinkNestManualHandlerOptions[];
  readonly sendHandlers?: readonly ZLinkChannelSendHandlerRegistration[];
  readonly sendHandlerTypes?: readonly ZLinkNestManualHandlerOptions[];
}

interface ZLinkNestFanoutChannelOptions extends ZLinkNestHandlerDiscoveryOptions {
  readonly publisher?: ZLinkPublisherCapabilityOptions;
  readonly subscriber?: ZLinkClientCapabilityOptions;
  readonly publishHandlers?: readonly ZLinkChannelPublishHandlerRegistration[];
  readonly publishHandlerTypes?: readonly ZLinkNestManualHandlerOptions[];
}

interface ZLinkNestDealerMeshChannelOptions extends ZLinkDealerMeshChannelOptions {}

interface ZLinkNestRouterMeshOptions extends ZLinkNestHandlerDiscoveryOptions {
  readonly bind?: string;
  readonly manualConnections?: readonly string[];
  readonly routingId?: string;
  readonly sendHandlers?: readonly ZLinkRouteChannelSendHandlerRegistration[];
  readonly requestHandlers?: readonly ZLinkRouteChannelRequestHandlerRegistration[];
  readonly sendHandlerTypes?: readonly ZLinkNestManualHandlerOptions[];
  readonly requestHandlerTypes?: readonly ZLinkNestManualHandlerOptions[];
  readonly handlers?: ZLinkRouteChannelOptions['handlers'];
}

export type ZLinkNestHandlerKind = 'request' | 'send' | 'publish';

export interface ZLinkNestHandlerOptions {
  readonly methodName?: string;
  readonly decodePayload?: (
    payload: Buffer,
    context: ZLinkRequestContext | ZLinkSendContext | ZLinkRouteRequestContext | ZLinkRouteSendContext | ZLinkPublishContext
  ) => unknown;
  readonly encodeResult?: (
    result: unknown,
    context: ZLinkRequestContext | ZLinkRouteRequestContext
  ) => unknown;
}

export interface ZLinkNestProviderDiscoveryOptions {
  readonly recursive?: boolean;
}

export type ZLinkNestProviderDiscoveryRoot =
  | string
  | {
      readonly rootDir: string;
      readonly options?: ZLinkNestProviderDiscoveryOptions;
    };

export interface ZLinkNestModuleMetadata extends ModuleMetadata {
  readonly providerDiscovery?: readonly ZLinkNestProviderDiscoveryRoot[];
}

export type ZLinkNestModuleRoleRoot = string;

export type ZLinkNestTypeResolver<T> = Type<T> | (() => Type<T>);

export interface ZLinkNestSpotActorSendHandlerOptions<TSpot extends ZLinkSpot, TActor extends ZLinkActor> {
  readonly spot: ZLinkNestTypeResolver<TSpot>;
  readonly actor: ZLinkNestTypeResolver<TActor>;
  readonly packetName: string;
  readonly methodName?: string;
}

export interface ZLinkNestSpotActorRequestHandlerOptions<TSpot extends ZLinkSpot, TActor extends ZLinkActor> {
  readonly spot: ZLinkNestTypeResolver<TSpot>;
  readonly actor: ZLinkNestTypeResolver<TActor>;
  readonly packetName: string;
  readonly methodName?: string;
}

export interface ZLinkNestEntrySpotActorRequestHandlerOptions<TEntrySpot extends ZLinkEntrySpot, TActor extends ZLinkActor> {
  readonly entrySpot: ZLinkNestTypeResolver<TEntrySpot>;
  readonly actor: ZLinkNestTypeResolver<TActor>;
  readonly packetName: string;
  readonly methodName?: string;
}

export interface ZLinkNestEntrySpotActorSendHandlerOptions<TEntrySpot extends ZLinkEntrySpot, TActor extends ZLinkActor> {
  readonly entrySpot: ZLinkNestTypeResolver<TEntrySpot>;
  readonly actor: ZLinkNestTypeResolver<TActor>;
  readonly packetName: string;
  readonly methodName?: string;
}

export interface ZLinkNestSpotTimerHandlerOptions<TSpot extends ZLinkSpot = ZLinkSpot> {
  readonly spot?: ZLinkNestTypeResolver<TSpot>;
  readonly entrySpot?: ZLinkNestTypeResolver<ZLinkEntrySpot>;
  readonly name?: string;
  readonly periodMs?: number;
  readonly options?: ZLinkTimerOptions;
}

const ZLINK_MODULE_OPTIONS_BRAND = Symbol('@zlink-systems/nestjs:module-options');

export interface ZLinkModuleOptions {
  readonly [ZLINK_MODULE_OPTIONS_BRAND]: true;
}

interface ZLinkNestModuleRegistrationOptions extends Omit<
  ZLinkFrameworkRegistrationOptions,
  'channels' | 'routeChannels' | 'streamNodes' | 'spotNodes'
> {
  readonly [ZLINK_MODULE_OPTIONS_BRAND]: true;
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
  codecs(): ZLinkNestCodecRegistryBuilder;
  configureDispatch(): ZLinkDispatchOptionsBuilder;
  actorFactory(actorType: string, factoryType: Type): this;
  useDiscovery(): ZLinkNestDiscoveryBuilder;
  addClientServerChannel(name: string): ZLinkNestClientServerChannelBuilder;
  addFanoutChannel(name: string): ZLinkNestFanoutChannelBuilder;
  addDealerMeshChannel(name: string): ZLinkNestDealerMeshChannelBuilder;
  addRouteMeshChannel(name: string): ZLinkNestRouterMeshBuilder;
  addSpotNode(name: string): ZLinkNestSpotNodeBuilder;
  addStreamNode(name: string): ZLinkNestStreamNodeBuilder;
  build(): ZLinkModuleOptions;
}

export type ZLinkNestFrameworkAdditionalOptions = Omit<
  ZLinkFrameworkRegistrationOptions,
  'channels' | 'routeChannels' | 'streamNodes' | 'spotNodes' | 'codecs'
>;

export interface ZLinkNestDiscoveryBuilder extends ZLinkNestFrameworkOptionsBuilder {
  addRegistryEndpoint(endpoint: string): this;
}

export interface ZLinkNestCodecRegistryBuilder extends ZLinkNestFrameworkOptionsBuilder {
  use(extension: ZLinkCodecExtension): this;
  addSerializer(contentType: string, serializer: ZLinkMessageSerializer): this;
  addStreamCodec(contentType: string, codec: unknown): this;
  addJson(): this;
}

export interface ZLinkNestClientServerChannelBuilder extends ZLinkNestFrameworkOptionsBuilder {
  enableServer(bind: string | undefined): this;
  enableClient(endpoint?: string | readonly string[]): this;
  addRequestHandler(packetName: string, handlerType: Type): this;
  addSendHandler(packetName: string, handlerType: Type): this;
  addHandlerGroup(groupName: string): this;
}

export interface ZLinkNestFanoutChannelBuilder extends ZLinkNestFrameworkOptionsBuilder {
  enablePublisher(bind: string | undefined): this;
  enableSubscriber(endpoint?: string | readonly string[]): this;
  addPublishHandler(packetName: string, handlerType: Type): this;
  addHandlerGroup(groupName: string): this;
}

export interface ZLinkNestDealerMeshChannelBuilder extends ZLinkNestFrameworkOptionsBuilder {
  bind(endpoint: string | undefined): this;
  enableClient(endpoint?: string | readonly string[]): this;
}

export interface ZLinkNestRouterMeshBuilder extends ZLinkNestFrameworkOptionsBuilder {
  enableRouter(endpoint: string | undefined): this;
  routingId(routingId: string | undefined): this;
  connect(endpoint: string | readonly string[] | undefined): this;
  addSendHandler(packetName: string, handlerType: Type): this;
  addRequestHandler(packetName: string, handlerType: Type): this;
  addHandlerGroup(groupName: string): this;
}

export interface ZLinkNestStreamNodeBuilder extends ZLinkNestFrameworkOptionsBuilder {
  bind(endpoint: string | undefined): this;
  attachActorGateway(spotNodeName: string | undefined): this;
  registerSession<TSession extends ZLinkSession>(sessionType: Type<TSession> | Type<ZLinkSessionFactory<TSession>>): this;
}

export interface ZLinkNestSpotNodeBuilder extends ZLinkNestFrameworkOptionsBuilder {
  enableRouter(bind: string | undefined, routingId?: string, connect?: string | readonly string[]): this;
  connectRouter(endpoint: string): this;
  connectRouter(peerRid: string, endpoint: string): this;
  enablePubSub(bind: string | undefined, routingId?: string, connect?: string | readonly string[]): this;
  connectPeerPub(endpoint: string): this;
  connectPubSub(endpoint: string): this;
  configureEntrySpot(options: ZLinkEntrySpotOptions): this;
  attachChannelClient(channelName: string, endpoint?: string | readonly string[]): this;
  attachSpotPublisherClient(channelName: string, endpoint?: string | readonly string[]): this;
  acceptSpotRoutesFromChannel(channelName: string, endpoint?: string | readonly string[]): this;
  acceptSpotRoutesFromChannel(channelName: string, peerRid: string, endpoint: string): this;
  addEntrySpot<TEntrySpot extends ZLinkEntrySpot>(entrySpotType: Type<TEntrySpot>): this;
  addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this;
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
const nestSpotActorHandlerMetadataByToken = new Map<unknown, readonly ZLinkNestSpotActorHandlerMetadata[]>();
const nestSpotTimerHandlerMetadataByToken = new Map<unknown, readonly ZLinkNestSpotTimerHandlerMetadata[]>();
const nestSpotTimerHandlerTokens = new Set<unknown>();

export function zlinkFramework(): ZLinkNestFrameworkOptionsBuilder {
  return new DefaultZLinkNestFrameworkOptionsBuilder();
}

export function zlinkRequestHandler(
  groupName: string,
  packetName?: string,
  options: ZLinkNestHandlerOptions = {}
): ClassDecorator {
  return zlinkHandler(groupName, 'request', packetName, options);
}

export function zlinkSendHandler(
  groupName: string,
  packetName?: string,
  options: ZLinkNestHandlerOptions = {}
): ClassDecorator {
  return zlinkHandler(groupName, 'send', packetName, options);
}

export function zlinkPublishHandler(
  groupName: string,
  packetName?: string,
  options: ZLinkNestHandlerOptions = {}
): ClassDecorator {
  return zlinkHandler(groupName, 'publish', packetName, options);
}

export function zlinkDiscoverProviders(
  rootDir: string,
  options: ZLinkNestProviderDiscoveryOptions = {}
): Provider[] {
  return [...loadDecoratedProviderModules(rootDir, options)];
}

export function zlinkModule(metadata: ZLinkNestModuleMetadata): ClassDecorator;
export function zlinkModule(roleRoot: ZLinkNestModuleRoleRoot, metadata: ModuleMetadata): ClassDecorator;
export function zlinkModule(
  metadataOrRoleRoot: ZLinkNestModuleMetadata | ZLinkNestModuleRoleRoot,
  metadata?: ModuleMetadata
): ClassDecorator {
  const moduleMetadata = typeof metadataOrRoleRoot === 'string' ? metadata : metadataOrRoleRoot;
  if (moduleMetadata === undefined) {
    throw new framework.ZLinkConfigurationException('zlinkModule metadata is required.');
  }
  const { providerDiscovery, providers, ...rest } = moduleMetadata as ZLinkNestModuleMetadata;
  const discoveredProviders = typeof metadataOrRoleRoot === 'string'
    ? createDefaultProviderDiscoveryProviders(metadataOrRoleRoot)
    : createProviderDiscoveryProviders(providerDiscovery);
  return Module({
    ...rest,
    providers: [
      ...(providers ?? []),
      ...discoveredProviders
    ]
  });
}

export function zlinkSpotActorRequestHandler<TSpot extends ZLinkSpot, TActor extends ZLinkActor>(
  options: ZLinkNestSpotActorRequestHandlerOptions<TSpot, TActor>
): ClassDecorator {
  return (target: Function) => {
    Injectable()(target as Type);
    appendNestSpotActorHandlerMetadata(target as Type, {
      actor: options.actor,
      handlerType: target as Type,
      kind: 'spotActorRequest',
      methodName: options.methodName ?? 'handle',
      packetName: options.packetName,
      spot: options.spot
    });
  };
}

export function zlinkSpotActorSendHandler<TSpot extends ZLinkSpot, TActor extends ZLinkActor>(
  options: ZLinkNestSpotActorSendHandlerOptions<TSpot, TActor>
): ClassDecorator {
  return (target: Function) => {
    Injectable()(target as Type);
    appendNestSpotActorHandlerMetadata(target as Type, {
      actor: options.actor,
      handlerType: target as Type,
      kind: 'spotActorSend',
      methodName: options.methodName ?? 'handle',
      packetName: options.packetName,
      spot: options.spot
    });
  };
}

export function zlinkEntrySpotActorRequestHandler<TEntrySpot extends ZLinkEntrySpot, TActor extends ZLinkActor>(
  options: ZLinkNestEntrySpotActorRequestHandlerOptions<TEntrySpot, TActor>
): ClassDecorator {
  return (target: Function) => {
    Injectable()(target as Type);
    appendNestSpotActorHandlerMetadata(target as Type, {
      actor: options.actor,
      entrySpot: options.entrySpot,
      handlerType: target as Type,
      kind: 'entrySpotActorRequest',
      methodName: options.methodName ?? 'handle',
      packetName: options.packetName
    });
  };
}

export function zlinkEntrySpotActorSendHandler<TEntrySpot extends ZLinkEntrySpot, TActor extends ZLinkActor>(
  options: ZLinkNestEntrySpotActorSendHandlerOptions<TEntrySpot, TActor>
): ClassDecorator {
  return (target: Function) => {
    Injectable()(target as Type);
    appendNestSpotActorHandlerMetadata(target as Type, {
      actor: options.actor,
      entrySpot: options.entrySpot,
      handlerType: target as Type,
      kind: 'entrySpotActorSend',
      methodName: options.methodName ?? 'handle',
      packetName: options.packetName
    });
  };
}

export function zlinkSpotTimerHandler<TSpot extends ZLinkSpot = ZLinkSpot>(
  options: ZLinkNestSpotTimerHandlerOptions<TSpot> = {}
): ClassDecorator {
  return (target: Function) => {
    Injectable()(target as Type);
    nestSpotTimerHandlerTokens.add(target);
    if (options.name !== undefined && options.periodMs !== undefined) {
      appendNestSpotTimerHandlerMetadata(target as Type, {
        entrySpot: options.entrySpot,
        handlerType: target as Type,
        name: options.name,
        options: options.options,
        periodMs: options.periodMs,
        spot: options.spot
      });
    }
  };
}

export function zlinkHandler(
  groupName: string,
  kind: ZLinkNestHandlerKind,
  packetName?: string,
  options: ZLinkNestHandlerOptions = {}
): ClassDecorator {
  validateHandlerGroupName(groupName);
  return (target: Function) => {
    Injectable()(target as Type);
    appendNestHandlerMetadata(target as Type, {
      decodePayload: options.decodePayload,
      encodeResult: options.encodeResult,
      groupName,
      kind,
      methodName: options.methodName ?? 'handle',
      packetName: packetName ?? inferPacketName(target as Type, target as Type)
    });
  };
}

class DefaultZLinkNestFrameworkOptionsBuilder implements ZLinkNestFrameworkOptionsBuilder {
  private additionalOptions: ZLinkNestFrameworkAdditionalOptions = {};
  private readonly clientServerChannels: Record<string, ZLinkNestClientServerChannelOptions> = {};
  private readonly fanoutChannels: Record<string, ZLinkNestFanoutChannelOptions> = {};
  private readonly dealerMeshChannels: Record<string, ZLinkNestDealerMeshChannelOptions> = {};
  private readonly routerMeshes: Record<string, ZLinkNestRouterMeshOptions> = {};
  private readonly streams: Record<string, ZLinkStreamNodeOptions> = {};
  private readonly spotNodes: Record<string, ZLinkSpotNodeOptions> = {};
  private readonly actorFactories: Record<string, Type> = {};
  private readonly codecOptions: MutableCodecRegistryOptions = { codecs: [], serializers: [], streamCodecs: [] };

  options(options: ZLinkNestFrameworkAdditionalOptions): this {
    this.additionalOptions = { ...this.additionalOptions, ...options };
    return this;
  }

  codecs(): ZLinkNestCodecRegistryBuilder {
    return new DefaultZLinkNestCodecRegistryBuilder(this);
  }

  configureDispatch(): ZLinkDispatchOptionsBuilder {
    this.additionalOptions = {
      ...this.additionalOptions,
      dispatch: this.additionalOptions.dispatch ?? {}
    };
    return new DefaultZLinkNestDispatchOptionsBuilder(
      this.additionalOptions.dispatch as NonNullable<ZLinkFrameworkRegistrationOptions['dispatch']>
    );
  }

  addNamedCodec(codec: ZLinkNamedCodec): void {
    if (!this.codecOptions.codecs.includes(codec)) {
      this.codecOptions.codecs.push(codec);
    }
  }

  addSerializer(contentType: string, serializer: ZLinkMessageSerializer): void {
    const existing = this.codecOptions.serializers.findIndex((entry) => entry.contentType === contentType);
    const registration = { contentType, serializer };
    if (existing >= 0) {
      this.codecOptions.serializers[existing] = registration;
    } else {
      this.codecOptions.serializers.push(registration);
    }
  }

  addStreamCodec(contentType: string, codec: unknown): void {
    const existing = this.codecOptions.streamCodecs.findIndex((entry) => entry.contentType === contentType);
    const registration = { contentType, codec };
    if (existing >= 0) {
      this.codecOptions.streamCodecs[existing] = registration;
    } else {
      this.codecOptions.streamCodecs.push(registration);
    }
  }

  actorFactory(actorType: string, factoryType: Type): this {
    this.actorFactories[actorType] = factoryType;
    return this;
  }

  useDiscovery(): ZLinkNestDiscoveryBuilder {
    this.additionalOptions = {
      ...this.additionalOptions,
      discovery: this.additionalOptions.discovery ?? { registries: [] }
    };
    return new DefaultZLinkNestDiscoveryBuilder(this, this.additionalOptions.discovery as { registries?: readonly string[] });
  }

  addClientServerChannel(name: string): ZLinkNestClientServerChannelBuilder {
    this.clientServerChannels[name] ??= {};
    return new DefaultZLinkNestClientServerChannelBuilder(this, this.clientServerChannels[name]);
  }

  addFanoutChannel(name: string): ZLinkNestFanoutChannelBuilder {
    this.fanoutChannels[name] ??= {};
    return new DefaultZLinkNestFanoutChannelBuilder(this, this.fanoutChannels[name]);
  }

  addDealerMeshChannel(name: string): ZLinkNestDealerMeshChannelBuilder {
    this.dealerMeshChannels[name] ??= {};
    return new DefaultZLinkNestDealerMeshChannelBuilder(this, this.dealerMeshChannels[name]);
  }

  addRouteMeshChannel(name: string): ZLinkNestRouterMeshBuilder {
    this.routerMeshes[name] ??= {};
    return new DefaultZLinkNestRouterMeshBuilder(this, this.routerMeshes[name]);
  }

  addSpotNode(name: string): ZLinkNestSpotNodeBuilder {
    this.spotNodes[name] ??= {};
    return new DefaultZLinkNestSpotNodeBuilder(this, this.spotNodes[name]);
  }

  addStreamNode(name: string): ZLinkNestStreamNodeBuilder {
    this.streams[name] ??= {};
    return new DefaultZLinkNestStreamNodeBuilder(this, this.streams[name]);
  }

  build(): ZLinkModuleOptions {
    const options: ZLinkNestModuleRegistrationOptions = {
      [ZLINK_MODULE_OPTIONS_BRAND]: true,
      ...this.additionalOptions,
      clientServerChannels: { ...this.clientServerChannels },
      fanoutChannels: { ...this.fanoutChannels },
      dealerMeshChannels: { ...this.dealerMeshChannels },
      routerMeshes: { ...this.routerMeshes },
      streams: { ...this.streams },
      spotNodes: { ...this.spotNodes },
      actorFactories: { ...this.actorFactories, ...(this.additionalOptions.actorFactories as Record<string, Type> | undefined ?? {}) },
      codecs: this.codecOptions.codecs.length === 0 &&
          this.codecOptions.serializers.length === 0 &&
          this.codecOptions.streamCodecs.length === 0
        ? undefined
        : {
            codecs: [...this.codecOptions.codecs],
            serializers: [...this.codecOptions.serializers],
            streamCodecs: [...this.codecOptions.streamCodecs]
          }
    };
    return options;
  }
}

abstract class ZLinkNestChildBuilder implements ZLinkNestFrameworkOptionsBuilder {
  protected constructor(protected readonly root: DefaultZLinkNestFrameworkOptionsBuilder) {}

  options(options: ZLinkNestFrameworkAdditionalOptions): this {
    this.root.options(options);
    return this;
  }

  codecs(): ZLinkNestCodecRegistryBuilder {
    return this.root.codecs();
  }

  configureDispatch(): ZLinkDispatchOptionsBuilder {
    return this.root.configureDispatch();
  }

  actorFactory(actorType: string, factoryType: Type): this {
    this.root.actorFactory(actorType, factoryType);
    return this;
  }

  useDiscovery(): ZLinkNestDiscoveryBuilder {
    return this.root.useDiscovery();
  }

  addClientServerChannel(name: string): ZLinkNestClientServerChannelBuilder {
    return this.root.addClientServerChannel(name);
  }

  addFanoutChannel(name: string): ZLinkNestFanoutChannelBuilder {
    return this.root.addFanoutChannel(name);
  }

  addDealerMeshChannel(name: string): ZLinkNestDealerMeshChannelBuilder {
    return this.root.addDealerMeshChannel(name);
  }

  addRouteMeshChannel(name: string): ZLinkNestRouterMeshBuilder {
    return this.root.addRouteMeshChannel(name);
  }

  addSpotNode(name: string): ZLinkNestSpotNodeBuilder {
    return this.root.addSpotNode(name);
  }

  addStreamNode(name: string): ZLinkNestStreamNodeBuilder {
    return this.root.addStreamNode(name);
  }

  build(): ZLinkModuleOptions {
    return this.root.build();
  }
}

class DefaultZLinkNestDiscoveryBuilder extends ZLinkNestChildBuilder implements ZLinkNestDiscoveryBuilder {
  constructor(root: DefaultZLinkNestFrameworkOptionsBuilder, private readonly discovery: { registries?: readonly string[] }) {
    super(root);
  }

  addRegistryEndpoint(endpoint: string): this {
    this.discovery.registries = [...(this.discovery.registries ?? []), endpoint];
    return this;
  }
}

class DefaultZLinkNestDispatchOptionsBuilder implements ZLinkDispatchOptionsBuilder {
  constructor(private readonly dispatch: NonNullable<ZLinkFrameworkRegistrationOptions['dispatch']>) {}

  setMessageDispatchErrorObserver(observerType: Type<ZLinkMessageDispatchErrorObserver>): this {
    this.dispatch.messageDispatchErrorObserverType = observerType;
    return this;
  }
}

class DefaultZLinkNestCodecRegistryBuilder extends ZLinkNestChildBuilder implements ZLinkNestCodecRegistryBuilder {
  constructor(root: DefaultZLinkNestFrameworkOptionsBuilder) {
    super(root);
  }

  addSerializer(contentType: string, serializer: ZLinkMessageSerializer): this {
    this.root.addSerializer(contentType, serializer);
    return this;
  }

  addStreamCodec(contentType: string, codec: unknown): this {
    if (contentType.trim().length === 0) {
      throw new Error('Codec content type must not be empty.');
    }
    this.root.addStreamCodec(contentType, codec);
    return this;
  }

  use(extension: ZLinkCodecExtension): this {
    extension.register(this);
    return this;
  }

  addJson(): this {
    this.root.addNamedCodec('json');
    return this;
  }
}

class DefaultZLinkNestClientServerChannelBuilder extends ZLinkNestChildBuilder implements ZLinkNestClientServerChannelBuilder {
  constructor(root: DefaultZLinkNestFrameworkOptionsBuilder, private readonly channelOptions: Mutable<ZLinkNestClientServerChannelOptions>) {
    super(root);
  }

  enableServer(bind: string | undefined): this {
    this.channelOptions.server = { bind };
    return this;
  }

  enableClient(endpoint?: string | readonly string[]): this {
    this.channelOptions.client = endpoint === undefined ? {} : { manualConnections: endpointList(endpoint) };
    return this;
  }

  addHandlerGroup(groupName: string): this {
    this.channelOptions.handlerGroups = [...(this.channelOptions.handlerGroups ?? []), groupName];
    return this;
  }

  addRequestHandler(packetName: string, handlerType: Type): this {
    this.channelOptions.requestHandlerTypes = [...(this.channelOptions.requestHandlerTypes ?? []), { packetName, handlerType }];
    return this;
  }

  addSendHandler(packetName: string, handlerType: Type): this {
    this.channelOptions.sendHandlerTypes = [...(this.channelOptions.sendHandlerTypes ?? []), { packetName, handlerType }];
    return this;
  }
}

class DefaultZLinkNestFanoutChannelBuilder extends ZLinkNestChildBuilder implements ZLinkNestFanoutChannelBuilder {
  constructor(root: DefaultZLinkNestFrameworkOptionsBuilder, private readonly channelOptions: Mutable<ZLinkNestFanoutChannelOptions>) {
    super(root);
  }

  enablePublisher(bind: string | undefined): this {
    this.channelOptions.publisher = { bind };
    return this;
  }

  enableSubscriber(endpoint?: string | readonly string[]): this {
    this.channelOptions.subscriber = endpoint === undefined ? {} : { manualConnections: endpointList(endpoint) };
    return this;
  }

  addHandlerGroup(groupName: string): this {
    this.channelOptions.handlerGroups = [...(this.channelOptions.handlerGroups ?? []), groupName];
    return this;
  }

  addPublishHandler(packetName: string, handlerType: Type): this {
    this.channelOptions.publishHandlerTypes = [...(this.channelOptions.publishHandlerTypes ?? []), { packetName, handlerType }];
    return this;
  }
}

class DefaultZLinkNestDealerMeshChannelBuilder extends ZLinkNestChildBuilder implements ZLinkNestDealerMeshChannelBuilder {
  constructor(root: DefaultZLinkNestFrameworkOptionsBuilder, private readonly channelOptions: Mutable<ZLinkNestDealerMeshChannelOptions>) {
    super(root);
  }

  bind(endpoint: string | undefined): this {
    this.channelOptions.bind = endpoint;
    return this;
  }

  enableClient(endpoint?: string | readonly string[]): this {
    this.channelOptions.client = endpoint === undefined ? {} : { manualConnections: endpointList(endpoint) };
    return this;
  }
}

class DefaultZLinkNestRouterMeshBuilder extends ZLinkNestChildBuilder implements ZLinkNestRouterMeshBuilder {
  constructor(root: DefaultZLinkNestFrameworkOptionsBuilder, private readonly routeOptions: Mutable<ZLinkNestRouterMeshOptions>) {
    super(root);
  }

  enableRouter(endpoint: string | undefined): this {
    this.routeOptions.bind = endpoint;
    return this;
  }

  routingId(routingId: string | undefined): this {
    this.routeOptions.routingId = routingId;
    return this;
  }

  connect(endpoint: string | readonly string[] | undefined): this {
    this.routeOptions.manualConnections = endpoint === undefined ? [] : endpointList(endpoint);
    return this;
  }

  addHandlerGroup(groupName: string): this {
    this.routeOptions.handlerGroups = [...(this.routeOptions.handlerGroups ?? []), groupName];
    return this;
  }

  addSendHandler(packetName: string, handlerType: Type): this {
    this.routeOptions.sendHandlerTypes = [...(this.routeOptions.sendHandlerTypes ?? []), { packetName, handlerType }];
    return this;
  }

  addRequestHandler(packetName: string, handlerType: Type): this {
    this.routeOptions.requestHandlerTypes = [...(this.routeOptions.requestHandlerTypes ?? []), { packetName, handlerType }];
    return this;
  }
}

class DefaultZLinkNestStreamNodeBuilder extends ZLinkNestChildBuilder implements ZLinkNestStreamNodeBuilder {
  constructor(root: DefaultZLinkNestFrameworkOptionsBuilder, private readonly streamOptions: Mutable<ZLinkStreamNodeOptions>) {
    super(root);
  }

  bind(endpoint: string | undefined): this {
    this.streamOptions.bind = endpoint;
    return this;
  }

  attachActorGateway(spotNodeName: string | undefined): this {
    this.streamOptions.attachActorGateway = spotNodeName;
    return this;
  }

  registerSession<TSession extends ZLinkSession>(sessionType: Type<TSession> | Type<ZLinkSessionFactory<TSession>>): this {
    if (this.streamOptions.session !== undefined) {
      throw new framework.ZLinkConfigurationException('STREAM node cannot register more than one header stream session.');
    }
    this.streamOptions.session = sessionType as Type;
    return this;
  }

}

class DefaultZLinkNestSpotNodeBuilder extends ZLinkNestChildBuilder implements ZLinkNestSpotNodeBuilder {
  constructor(root: DefaultZLinkNestFrameworkOptionsBuilder, private readonly spotOptions: Mutable<ZLinkSpotNodeOptions>) {
    super(root);
  }

  enableRouter(bind: string | undefined, routingId?: string, connect?: string | readonly string[]): this {
    this.spotOptions.router = {
      bind,
      routingId,
      manualConnections: connect === undefined ? undefined : endpointList(connect)
    };
    return this;
  }

  connectRouter(peerRidOrEndpoint: string, endpoint?: string): this {
    this.spotOptions.router ??= {};
    const router = this.spotOptions.router as {
      manualConnections?: string[];
      manualPeerConnections?: { peerRid: string; endpoint: string }[];
    };
    if (endpoint === undefined) {
      router.manualConnections ??= [];
      router.manualConnections.push(peerRidOrEndpoint);
      return this;
    }
    router.manualPeerConnections ??= [];
    router.manualPeerConnections.push({
      peerRid: peerRidOrEndpoint,
      endpoint
    });
    return this;
  }

  enablePubSub(bind: string | undefined, routingId?: string, connect?: string | readonly string[]): this {
    this.spotOptions.pubSub = {
      bind,
      routingId,
      manualConnections: connect === undefined ? undefined : endpointList(connect)
    };
    return this;
  }

  connectPeerPub(endpoint: string): this {
    this.spotOptions.pubSub ??= {};
    const pubSub = this.spotOptions.pubSub as {
      manualConnections?: string[];
    };
    pubSub.manualConnections ??= [];
    pubSub.manualConnections.push(endpoint);
    return this;
  }

  connectPubSub(endpoint: string): this {
    return this.connectPeerPub(endpoint);
  }

  configureEntrySpot(options: ZLinkEntrySpotOptions): this {
    this.spotOptions.entrySpot = { ...options };
    return this;
  }

  attachChannelClient(channelName: string, endpoint?: string | readonly string[]): this {
    this.spotOptions.attachedChannelClients = {
      ...(this.spotOptions.attachedChannelClients ?? {}),
      [channelName]: endpoint === undefined ? {} : { manualConnections: endpointList(endpoint) }
    };
    return this;
  }

  attachSpotPublisherClient(channelName: string, endpoint?: string | readonly string[]): this {
    this.spotOptions.attachedSpotPublisherClients = {
      ...(this.spotOptions.attachedSpotPublisherClients ?? {}),
      [channelName]: endpoint === undefined ? {} : { manualConnections: endpointList(endpoint) }
    };
    return this;
  }

  acceptSpotRoutesFromChannel(channelName: string, endpoint?: string | readonly string[]): this;
  acceptSpotRoutesFromChannel(channelName: string, peerRid: string, endpoint: string): this;
  acceptSpotRoutesFromChannel(channelName: string, peerRidOrEndpoint?: string | readonly string[], endpoint?: string): this {
    if (endpoint !== undefined && typeof peerRidOrEndpoint === 'string') {
      const existing = this.spotOptions.acceptedSpotRouteChannels?.[channelName];
      this.spotOptions.acceptedSpotRouteChannels = {
        ...(this.spotOptions.acceptedSpotRouteChannels ?? {}),
        [channelName]: {
          ...(existing ?? {}),
          manualPeerConnections: [
            ...(existing?.manualPeerConnections ?? []),
            { peerRid: peerRidOrEndpoint, endpoint }
          ]
        }
      };
      return this;
    }
    this.spotOptions.acceptedSpotRouteChannels = {
      ...(this.spotOptions.acceptedSpotRouteChannels ?? {}),
      [channelName]: peerRidOrEndpoint === undefined ? {} : { manualConnections: endpointList(peerRidOrEndpoint) }
    };
    return this;
  }

  addEntrySpot<TEntrySpot extends ZLinkEntrySpot>(entrySpotType: Type<TEntrySpot>): this {
    this.spotOptions.entrySpotType = entrySpotType;
    return this;
  }

  addSpotFactory<TSpot extends ZLinkSpot>(spotType: Type<TSpot>): this {
    this.spotOptions.spotFactories = [...(this.spotOptions.spotFactories ?? []), spotType];
    return this;
  }
}

function endpointList(endpoint: string | readonly string[]): string[] {
  return typeof endpoint === 'string' ? [endpoint] : [...endpoint];
}

function validateHandlerGroupName(groupName: string): void {
  if (groupName.trim() === '') {
    throw new framework.ZLinkConfigurationException('ZLink handler group name must not be empty.');
  }
}

interface ZLinkNestHandlerMetadata {
  readonly groupName: string;
  readonly kind: ZLinkNestHandlerKind;
  readonly packetName: string;
  readonly methodName: string;
  readonly decodePayload?: ZLinkNestHandlerOptions['decodePayload'];
  readonly encodeResult?: ZLinkNestHandlerOptions['encodeResult'];
}

type ZLinkNestSpotActorHandlerKind =
  | 'spotActorSend'
  | 'spotActorRequest'
  | 'entrySpotActorSend'
  | 'entrySpotActorRequest';

interface ZLinkNestSpotActorHandlerMetadata {
  readonly kind: ZLinkNestSpotActorHandlerKind;
  readonly packetName: string;
  readonly methodName: string;
  readonly handlerType: Type;
  readonly actor: ZLinkNestTypeResolver<ZLinkActor>;
  readonly spot?: ZLinkNestTypeResolver<ZLinkSpot>;
  readonly entrySpot?: ZLinkNestTypeResolver<ZLinkEntrySpot>;
}

interface ZLinkNestSpotTimerHandlerMetadata {
  readonly handlerType: Type;
  readonly name: string;
  readonly periodMs: number;
  readonly options?: ZLinkTimerOptions;
  readonly spot?: ZLinkNestTypeResolver<ZLinkSpot>;
  readonly entrySpot?: ZLinkNestTypeResolver<ZLinkEntrySpot>;
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

function appendNestSpotActorHandlerMetadata(handlerToken: InjectionToken, metadata: ZLinkNestSpotActorHandlerMetadata): void {
  const current = readNestSpotActorHandlerMetadata(handlerToken);
  nestSpotActorHandlerMetadataByToken.set(handlerToken, [...current, metadata]);
}

function readNestSpotActorHandlerMetadata(handlerToken: InjectionToken | undefined): readonly ZLinkNestSpotActorHandlerMetadata[] {
  if (handlerToken === undefined) {
    return [];
  }
  return nestSpotActorHandlerMetadataByToken.get(handlerToken) ?? [];
}

function appendNestSpotTimerHandlerMetadata(handlerToken: InjectionToken, metadata: ZLinkNestSpotTimerHandlerMetadata): void {
  const current = readNestSpotTimerHandlerMetadata(handlerToken);
  nestSpotTimerHandlerMetadataByToken.set(handlerToken, [...current, metadata]);
}

function readNestSpotTimerHandlerMetadata(handlerToken: InjectionToken | undefined): readonly ZLinkNestSpotTimerHandlerMetadata[] {
  if (handlerToken === undefined) {
    return [];
  }
  return nestSpotTimerHandlerMetadataByToken.get(handlerToken) ?? [];
}

function hasNestSpotTimerHandlerMetadata(handlerToken: InjectionToken | undefined): boolean {
  return handlerToken !== undefined && nestSpotTimerHandlerTokens.has(handlerToken);
}

function loadDecoratedProviderModules(rootDir: string, options: ZLinkNestProviderDiscoveryOptions): Set<Type> {
  if (!fs.existsSync(rootDir)) {
    throw new framework.ZLinkConfigurationException(`ZLink provider discovery root does not exist: ${rootDir}`);
  }
  const providers = new Set<Type>();
  const stat = fs.statSync(rootDir);
  if (stat.isFile()) {
    addDecoratedProviderModuleExports(providers, rootDir);
    return providers;
  }
  for (const entry of fs.readdirSync(rootDir, { withFileTypes: true })) {
    const fullPath = path.join(rootDir, entry.name);
    if (entry.isDirectory()) {
      if (options.recursive === true) {
        for (const provider of loadDecoratedProviderModules(fullPath, options)) {
          providers.add(provider);
        }
      }
      continue;
    }
    if (entry.isFile()) {
      addDecoratedProviderModuleExports(providers, fullPath);
    }
  }
  return providers;
}

function addDecoratedProviderModuleExports(providers: Set<Type>, filePath: string): void {
  if (!/\.(?:cjs|mjs|js)$/.test(filePath) || /\.d\.js$/.test(filePath)) {
    return;
  }
  const loaded = createRequire(__filename)(filePath) as Record<string, unknown>;
  for (const value of Object.values(loaded)) {
    if (
      typeof value === 'function'
      && (
        readNestHandlerMetadata(value as Type).length > 0
        || readNestSpotActorHandlerMetadata(value as Type).length > 0
        || hasNestSpotTimerHandlerMetadata(value as Type)
      )
    ) {
      providers.add(value as Type);
    }
  }
}

@Module({})
export class ZLinkModule {
  static forRoot(options: ZLinkModuleOptions = zlinkFramework().build()): DynamicModule {
    const resolvedOptions = assertBuiltModuleOptions(options);
    if (hasNestHandlerDiscovery(resolvedOptions)) {
      return createDiscoveringZLinkDynamicModule(resolvedOptions);
    }
    return createZLinkDynamicModule(framework.createFrameworkRegistration(createRegistrationOptions(resolvedOptions)));
  }

  static forRootFactory(options: ZLinkModuleFactoryOptions): DynamicModule {
    const registrationProvider: Provider<Promise<ZLinkFrameworkRegistration>> = {
      provide: ZLINK_FRAMEWORK_REGISTRATION,
      inject: [...(options.inject ?? []), DiscoveryService, ModuleRef],
      useFactory: async (...args: unknown[]) => {
        const discovery = args[args.length - 2] as DiscoveryService;
        const moduleRef = args[args.length - 1] as ModuleRef;
        const factoryArgs = args.slice(0, -2);
        const resolvedOptions = assertBuiltModuleOptions(await options.useFactory(...factoryArgs));
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
        ...conditionalClientProvidersForFactory()
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

  static forRootFactory(options: ZLinkRegistryModuleFactoryOptions): DynamicModule {
    return createRegistryDynamicModuleFromFactory({
      module: ZLinkRegistryModule,
      options,
      runtimeToken: ZLINK_REGISTRY_RUNTIME,
      queryToken: ZLINK_REGISTRY_QUERY,
      createRuntime: (registration: ZLinkRegistryOptions) =>
        new framework.ZLinkRegistryRuntime({ registration }),
      createQuery: (runtime: RegistryRuntime) =>
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

  static forRootFactory(options: ZLinkRegistryQueryClientModuleFactoryOptions): DynamicModule {
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

function createRegistryDynamicModuleFromFactory(options: {
  readonly module: Type;
  readonly options: ZLinkRegistryModuleFactoryOptions;
  readonly runtimeToken: InjectionToken;
  readonly queryToken: InjectionToken;
  readonly createRuntime: (registration: ZLinkRegistryOptions) => RegistryRuntime;
  readonly createQuery: (
    runtime: RegistryRuntime
  ) => unknown;
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

function createDiscoveringZLinkDynamicModule(options: ZLinkNestModuleRegistrationOptions): DynamicModule {
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
      ...conditionalClientProvidersForFactory()
    ],
    exports: [
      ZLINK_FRAMEWORK_RUNTIME,
      ...alwaysAvailableClientTokens(),
      ...conditionalClientTokens()
    ]
  };
}

function createProviderDiscoveryProviders(
  roots: readonly ZLinkNestProviderDiscoveryRoot[] | undefined
): Provider[] {
  return (roots ?? []).flatMap((root) => {
    if (typeof root === 'string') {
      return zlinkDiscoverProviders(root);
    }
    return zlinkDiscoverProviders(root.rootDir, root.options);
  });
}

function createDefaultProviderDiscoveryProviders(roleRoot: string): Provider[] {
  return createProviderDiscoveryProviders(defaultProviderDiscoveryRoots(roleRoot));
}

function defaultProviderDiscoveryRoots(roleRoot: string): ZLinkNestProviderDiscoveryRoot[] {
  return [
    path.join(roleRoot, 'Handlers'),
    path.join(roleRoot, 'Adapters', 'ZLink', 'Handlers'),
    path.join(roleRoot, 'Adapters', 'ZLink', 'Spots', 'Handlers')
  ].filter((rootDir) => fs.existsSync(rootDir));
}

function createDiscoveredOptions(
  options: ZLinkNestModuleRegistrationOptions,
  discovery: DiscoveryService,
  moduleRef: ModuleRef
): ZLinkFrameworkRegistrationOptions {
  const registrationOptions = createRegistrationOptions(options);
  const channels: Record<string, ZLinkChannelOptions> = { ...(registrationOptions.channels ?? {}) };
  const routerMeshes = new Map<string, ZLinkRouteChannelOptions>();
  const providerRefs = discoverProviderRefs(discovery, moduleRef);
  const spotActorProviderRefs = discoverSpotActorProviderRefs(discovery, moduleRef);
  const spotTimerProviderRefs = discoverSpotTimerProviderRefs(discovery, moduleRef);
  const spotNodes = createDiscoveredSpotNodeOptions(
    registrationOptions.spotNodes,
    spotActorProviderRefs,
    spotTimerProviderRefs);

  for (const [channelName, channel] of Object.entries(options.clientServerChannels ?? {})) {
    const existingChannel = channels[channelName] as ZLinkChannelOptions | undefined;
    const requestHandlers = createDiscoveredRequestHandlers(
      providerRefs,
      channel.handlerGroups,
      moduleRef
    );
    const sendHandlers = createDiscoveredSendHandlers(
      providerRefs,
      channel.handlerGroups,
      moduleRef
    );
    const manualRequestHandlers = createManualRequestHandlers(channel.requestHandlerTypes, moduleRef);
    const manualSendHandlers = createManualSendHandlers(channel.sendHandlerTypes, moduleRef);
    channels[channelName] = {
      ...existingChannel,
      requestHandlers: channel.server === undefined
        ? existingChannel?.requestHandlers
        : [
            ...(existingChannel?.requestHandlers ?? []),
            ...manualRequestHandlers,
            ...requestHandlers
          ],
      sendHandlers: channel.server === undefined
        ? existingChannel?.sendHandlers
        : [
            ...(existingChannel?.sendHandlers ?? []),
            ...manualSendHandlers,
            ...sendHandlers
          ]
    };
  }

  for (const [channelName, channel] of Object.entries(options.fanoutChannels ?? {})) {
    const existingChannel = channels[channelName] as ZLinkChannelOptions | undefined;
    const publishHandlers = createDiscoveredPublishHandlers(providerRefs, channel.handlerGroups, moduleRef);
    const manualPublishHandlers = createManualPublishHandlers(channel.publishHandlerTypes, moduleRef);
    channels[channelName] = {
      ...existingChannel,
      publishHandlers: [
        ...(existingChannel?.publishHandlers ?? []),
        ...manualPublishHandlers,
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
    const manualRequestHandlers = createManualRouteRequestHandlers(routerMesh.requestHandlerTypes, moduleRef);
    const manualSendHandlers = createManualRouteSendHandlers(routerMesh.sendHandlerTypes, moduleRef);
    routerMeshes.set(routerMeshName, {
      ...existing,
      requestHandlers: [
        ...(existing.requestHandlers ?? []),
        ...manualRequestHandlers,
        ...requestHandlers
      ],
      sendHandlers: [
        ...(existing.sendHandlers ?? []),
        ...manualSendHandlers,
        ...sendHandlers
      ]
    });
  }

  return {
    ...registrationOptions,
    channels,
    routeChannels: [...routerMeshes.values()],
    spotNodes
  };
}

function createDiscoveredSpotNodeOptions(
  value: ZLinkFrameworkRegistrationOptions['spotNodes'],
  refs: readonly DiscoveredNestSpotActorProvider[],
  timerRefs: readonly DiscoveredNestSpotTimerProvider[] = []
): ZLinkFrameworkRegistrationOptions['spotNodes'] {
  if (refs.length === 0 && timerRefs.length === 0) {
    return value;
  }
  const spotNodes = toMutableSpotNodeRecord(value);
  const spotNodeEntries = Object.entries(spotNodes);
  if (spotNodeEntries.length === 0) {
    throw new framework.ZLinkConfigurationException('ZLink SPOT actor handlers require a registered SpotNode.');
  }

  for (const ref of timerRefs) {
    if (ref.metadata.entrySpot !== undefined) {
      const entrySpotType = resolveNestType(ref.metadata.entrySpot, 'entrySpot');
      const matches = spotNodeEntries.filter(([, spotNode]) => spotNode.entrySpotType === entrySpotType);
      if (matches.length === 0) {
        throw new framework.ZLinkConfigurationException(
          `ZLink Entry Spot timer handler '${ref.handlerName}' targets an Entry Spot that is not registered on any SpotNode.`
        );
      }
      for (const [, spotNode] of matches) {
        spotNode.entrySpotTimerHandlers = [
          ...(spotNode.entrySpotTimerHandlers ?? []),
          {
            entrySpotType,
            handlerType: ref.handlerKey,
            name: ref.metadata.name,
            options: ref.metadata.options,
            periodMs: ref.metadata.periodMs
          }
        ];
      }
      continue;
    }
    const spotType = resolveNestType(ref.metadata.spot, 'spot');
    const matches = spotNodeEntries.filter(([, spotNode]) => (spotNode.spotFactories ?? []).includes(spotType));
    if (matches.length === 0) {
      throw new framework.ZLinkConfigurationException(
        `ZLink SPOT timer handler '${ref.handlerName}' targets a Spot type that is not registered on any SpotNode.`
      );
    }
    for (const [, spotNode] of matches) {
      spotNode.spotTimerHandlers = [
        ...(spotNode.spotTimerHandlers ?? []),
        {
          handlerType: ref.handlerKey,
          name: ref.metadata.name,
          options: ref.metadata.options,
          periodMs: ref.metadata.periodMs,
          spotType
        }
      ];
    }
  }

  for (const ref of refs) {
    if (ref.metadata.kind === 'entrySpotActorSend' || ref.metadata.kind === 'entrySpotActorRequest') {
      const entrySpotType = resolveNestType(ref.metadata.entrySpot, 'entrySpot');
      const actorType = resolveNestType(ref.metadata.actor, 'actor');
      const matches = spotNodeEntries.filter(([, spotNode]) => spotNode.entrySpotType === entrySpotType);
      if (matches.length === 0) {
        throw new framework.ZLinkConfigurationException(
          `ZLink Entry Spot actor handler '${ref.handlerName}' targets an Entry Spot that is not registered on any SpotNode.`
        );
      }
      for (const [, spotNode] of matches) {
        const next = {
          actorType,
          entrySpotType,
          handlerType: ref.handlerKey,
          packetName: ref.metadata.packetName
        };
        if (ref.metadata.kind === 'entrySpotActorSend') {
          assertUniqueEntrySpotActorHandler(spotNode.entrySpotActorSendHandlers, next);
          spotNode.entrySpotActorSendHandlers = [
            ...(spotNode.entrySpotActorSendHandlers ?? []),
            next
          ];
        } else {
          assertUniqueEntrySpotActorHandler(spotNode.entrySpotActorRequestHandlers, next);
          spotNode.entrySpotActorRequestHandlers = [
            ...(spotNode.entrySpotActorRequestHandlers ?? []),
            next
          ];
        }
      }
      continue;
    }

    const spotType = resolveNestType(ref.metadata.spot, 'spot');
    const actorType = resolveNestType(ref.metadata.actor, 'actor');
    const matches = spotNodeEntries.filter(([, spotNode]) => (spotNode.spotFactories ?? []).includes(spotType));
    if (matches.length === 0) {
      throw new framework.ZLinkConfigurationException(
        `ZLink SPOT actor handler '${ref.handlerName}' targets a Spot type that is not registered on any SpotNode.`
      );
    }
    for (const [, spotNode] of matches) {
      const next = {
        actorType,
        handlerType: ref.handlerKey,
        packetName: ref.metadata.packetName,
        spotType
      };
      if (ref.metadata.kind === 'spotActorSend') {
        assertUniqueSpotActorHandler(spotNode.spotActorSendHandlers, next);
        spotNode.spotActorSendHandlers = [
          ...(spotNode.spotActorSendHandlers ?? []),
          next
        ];
      } else {
        assertUniqueSpotActorHandler(spotNode.spotActorRequestHandlers, next);
        spotNode.spotActorRequestHandlers = [
          ...(spotNode.spotActorRequestHandlers ?? []),
          next
        ];
      }
    }
  }

  return spotNodes;
}

function toMutableSpotNodeRecord(value: ZLinkFrameworkRegistrationOptions['spotNodes']): Record<string, Mutable<ZLinkSpotNodeOptions>> {
  if (value === undefined) {
    return {};
  }
  if (!Array.isArray(value)) {
    return Object.fromEntries(Object.entries(value).map(([name, spotNode]) => [name, { ...spotNode }]));
  }
  return Object.fromEntries(value.map((spotNode) => {
    if (typeof spotNode === 'string') {
      return [spotNode, {}];
    }
    const { name, ...options } = spotNode;
    return [name, { ...options }];
  }));
}

function resolveNestType<T>(resolver: ZLinkNestTypeResolver<T> | undefined, name: string): Type<T> {
  if (resolver === undefined) {
    throw new framework.ZLinkConfigurationException(`ZLink SPOT actor handler ${name} type is required.`);
  }
  if (isClassType(resolver)) {
    return resolver;
  }
  const resolved = (resolver as () => Type<T>)();
  if (!isClassType(resolved)) {
    throw new framework.ZLinkConfigurationException(`ZLink SPOT actor handler ${name} type resolver must return a class.`);
  }
  return resolved;
}

function isClassType(value: unknown): value is Type {
  return typeof value === 'function' && /^class\s/.test(Function.prototype.toString.call(value));
}

function assertUniqueEntrySpotActorHandler(
  existing: readonly (ZLinkEntrySpotActorSendHandlerRegistration | ZLinkEntrySpotActorRequestHandlerRegistration)[] | undefined,
  next: ZLinkEntrySpotActorSendHandlerRegistration | ZLinkEntrySpotActorRequestHandlerRegistration
): void {
  if ((existing ?? []).some((handler) =>
    handler.entrySpotType === next.entrySpotType &&
    handler.actorType === next.actorType &&
    handler.packetName === next.packetName
  )) {
    throw new framework.ZLinkConfigurationException(
      `Duplicate Entry Spot actor handler '${next.entrySpotType.name}:${next.actorType.name}:${next.packetName}'.`
    );
  }
}

function assertUniqueSpotActorHandler(
  existing: readonly (ZLinkSpotActorSendHandlerRegistration | ZLinkSpotActorRequestHandlerRegistration)[] | undefined,
  next: ZLinkSpotActorSendHandlerRegistration | ZLinkSpotActorRequestHandlerRegistration
): void {
  if ((existing ?? []).some((handler) =>
    handler.spotType === next.spotType &&
    handler.actorType === next.actorType &&
    handler.packetName === next.packetName
  )) {
    throw new framework.ZLinkConfigurationException(
      `Duplicate SPOT actor handler '${next.spotType.name}:${next.actorType.name}:${next.packetName}'.`
    );
  }
}

function createRegistrationOptions(options: ZLinkNestModuleRegistrationOptions): ZLinkFrameworkRegistrationOptions {
  const channels: Record<string, ZLinkChannelOptions> = {};
  const routeChannels: ZLinkRouteChannelOptions[] = [];

  for (const [name, channel] of Object.entries(options.clientServerChannels ?? {})) {
    assertChannelNameAvailable(channels, name, 'ClientServerChannel');
    channels[name] = {
      client: channel.client,
      requestHandlers: channel.requestHandlers,
      sendHandlers: channel.sendHandlers,
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
    codecs: options.codecs,
    dispatch: options.dispatch,
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
  if (Object.hasOwn(channels, name)) {
    throw new framework.ZLinkConfigurationException(`Channel '${name}' is already registered before ${kind}.`);
  }
}

function assertBuiltModuleOptions(options: ZLinkModuleOptions): ZLinkNestModuleRegistrationOptions {
  if (!isBuiltModuleOptions(options)) {
    throw new framework.ZLinkConfigurationException('NestJS ZLinkModule options must be created with zlinkFramework().build().');
  }
  return options;
}

function isBuiltModuleOptions(options: unknown): options is ZLinkNestModuleRegistrationOptions {
  return typeof options === 'object'
    && options !== null
    && (options as { readonly [ZLINK_MODULE_OPTIONS_BRAND]?: unknown })[ZLINK_MODULE_OPTIONS_BRAND] === true;
}

interface DiscoveredNestProvider {
  readonly handlerKey: InjectionToken;
  readonly handlerName: string;
  readonly token: InjectionToken;
  readonly instance?: Record<string, unknown>;
  readonly metadata: ZLinkNestHandlerMetadata;
}

interface DiscoveredNestSpotActorProvider {
  readonly handlerKey: Type;
  readonly handlerName: string;
  readonly token: InjectionToken;
  readonly metadata: ZLinkNestSpotActorHandlerMetadata;
}

interface DiscoveredNestSpotTimerProvider {
  readonly handlerKey: Type;
  readonly handlerName: string;
  readonly token: InjectionToken;
  readonly metadata: ZLinkNestSpotTimerHandlerMetadata;
}

function createDiscoveredRequestHandlers(
  providerRefs: readonly DiscoveredNestProvider[],
  handlerGroups: readonly string[] | undefined,
  moduleRef: ModuleRef
): NonNullable<ZLinkChannelOptions['requestHandlers']> {
  return createDiscoveredHandlerRegistrations(providerRefs, handlerGroups, 'request', (ref, metadata) => ({
    async handle(payload: Buffer, context: ZLinkRequestContext) {
      const result = await invokeDiscoveredHandler(moduleRef, ref, metadata, payload, context);
      return encodeHandlerResult(metadata, result, context);
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

function createManualRequestHandlers(
  handlerTypes: readonly ZLinkNestManualHandlerOptions[] | undefined,
  moduleRef: ModuleRef
): NonNullable<ZLinkChannelOptions['requestHandlers']> {
  return (handlerTypes ?? []).map((registration) => ({
    packetName: registration.packetName,
    handler: {
      async handle(payload: Buffer, context: ZLinkRequestContext) {
        return await invokeManualHandler(moduleRef, registration.handlerType, payload, context);
      }
    }
  }));
}

function createManualPublishHandlers(
  handlerTypes: readonly ZLinkNestManualHandlerOptions[] | undefined,
  moduleRef: ModuleRef
): NonNullable<ZLinkChannelOptions['publishHandlers']> {
  return (handlerTypes ?? []).map((registration) => ({
    packetName: registration.packetName,
    handler: {
      async handle(payload: Buffer, context: ZLinkPublishContext) {
        await invokeManualHandler(moduleRef, registration.handlerType, payload, context);
      }
    }
  }));
}

function createManualSendHandlers(
  handlerTypes: readonly ZLinkNestManualHandlerOptions[] | undefined,
  moduleRef: ModuleRef
): NonNullable<ZLinkChannelOptions['sendHandlers']> {
  return (handlerTypes ?? []).map((registration) => ({
    packetName: registration.packetName,
    handler: {
      async handle(payload: Buffer, context: ZLinkSendContext) {
        await invokeManualHandler(moduleRef, registration.handlerType, payload, context);
      }
    }
  }));
}

function createManualRouteSendHandlers(
  handlerTypes: readonly ZLinkNestManualHandlerOptions[] | undefined,
  moduleRef: ModuleRef
): NonNullable<NonNullable<ZLinkChannelOptions['routeMesh']>['sendHandlers']> {
  return (handlerTypes ?? []).map((registration) => ({
    packetName: registration.packetName,
    handler: {
      async handle(payload: Buffer, context: ZLinkRouteSendContext) {
        await invokeManualHandler(moduleRef, registration.handlerType, payload, context);
      }
    }
  }));
}

function createManualRouteRequestHandlers(
  handlerTypes: readonly ZLinkNestManualHandlerOptions[] | undefined,
  moduleRef: ModuleRef
): NonNullable<NonNullable<ZLinkChannelOptions['routeMesh']>['requestHandlers']> {
  return (handlerTypes ?? []).map((registration) => ({
    packetName: registration.packetName,
    handler: {
      async handle(payload: Buffer, context: ZLinkRouteRequestContext) {
        return await invokeManualHandler(moduleRef, registration.handlerType, payload, context);
      }
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

function discoverSpotActorProviderRefs(discovery: DiscoveryService, moduleRef: ModuleRef): DiscoveredNestSpotActorProvider[] {
  const refs: DiscoveredNestSpotActorProvider[] = [];
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
      for (const metadata of readNestSpotActorHandlerMetadata(handlerKey)) {
        if (typeof handlerKey !== 'function') {
          throw new framework.ZLinkConfigurationException('ZLink SPOT actor handler decorators must be applied to class providers.');
        }
        const handlerName = handlerKeyName(handlerKey);
        const key = `${String(token)}:${handlerName}:${metadata.kind}:${metadata.packetName}`;
        if (seen.has(key)) {
          continue;
        }
        seen.add(key);
        refs.push({
          handlerKey: handlerKey as Type,
          handlerName,
          token,
          metadata
        });
      }
    }
  }

  for (const [handlerKey, metadataList] of nestSpotActorHandlerMetadataByToken) {
    if (typeof handlerKey !== 'function') {
      continue;
    }
    if (tryGetProviderInstance(moduleRef, handlerKey) === undefined) {
      continue;
    }
    const handlerName = handlerKeyName(handlerKey);
    for (const metadata of metadataList) {
      const key = `${String(handlerKey)}:${handlerName}:${metadata.kind}:${metadata.packetName}`;
      if (seen.has(key)) {
        continue;
      }
      seen.add(key);
      refs.push({
        handlerKey: handlerKey as Type,
        handlerName,
        token: handlerKey as Type,
        metadata
      });
    }
  }

  return refs;
}

function discoverSpotTimerProviderRefs(discovery: DiscoveryService, moduleRef: ModuleRef): DiscoveredNestSpotTimerProvider[] {
  const refs: DiscoveredNestSpotTimerProvider[] = [];
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
      for (const metadata of readNestSpotTimerHandlerMetadata(handlerKey)) {
        if (typeof handlerKey !== 'function') {
          throw new framework.ZLinkConfigurationException('ZLink SPOT timer handler decorators must be applied to class providers.');
        }
        const handlerName = handlerKeyName(handlerKey);
        const key = `${String(token)}:${handlerName}:${metadata.name}`;
        if (seen.has(key)) {
          continue;
        }
        seen.add(key);
        refs.push({
          handlerKey: handlerKey as Type,
          handlerName,
          token,
          metadata
        });
      }
    }
  }

  for (const [handlerKey, metadataList] of nestSpotTimerHandlerMetadataByToken) {
    if (typeof handlerKey !== 'function') {
      continue;
    }
    if (tryGetProviderInstance(moduleRef, handlerKey) === undefined) {
      continue;
    }
    const handlerName = handlerKeyName(handlerKey);
    for (const metadata of metadataList) {
      const key = `${String(handlerKey)}:${handlerName}:${metadata.name}`;
      if (seen.has(key)) {
        continue;
      }
      seen.add(key);
      refs.push({
        handlerKey: handlerKey as Type,
        handlerName,
        token: handlerKey as Type,
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
  context: ZLinkRequestContext | ZLinkSendContext | ZLinkRouteRequestContext | ZLinkRouteSendContext | ZLinkPublishContext
): Promise<unknown> {
  const instance = moduleRef.get(ref.token, { strict: false }) as Record<string, unknown>;
  const methodName = metadata.methodName;
  const method = instance[methodName];
  if (typeof method !== 'function') {
    throw new framework.ZLinkConfigurationException(
      `Discovered handler ${ref.handlerName}.${methodName} is not callable.`
    );
  }
  return await method.call(instance, decodePayload(metadata, payload, context), context);
}

async function invokeManualHandler(
  moduleRef: ModuleRef,
  handlerType: Type,
  payload: Buffer,
  context: ZLinkRequestContext | ZLinkSendContext | ZLinkRouteRequestContext | ZLinkRouteSendContext | ZLinkPublishContext
): Promise<unknown> {
  const instance = moduleRef.get(handlerType, { strict: false }) as Record<string, unknown>;
  const method = instance.handle;
  if (typeof method !== 'function') {
    throw new framework.ZLinkConfigurationException(
      `Manual handler ${handlerType.name}.handle is not callable.`
    );
  }
  return await method.call(instance, decodePayload(undefined, payload, context), context);
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

function encodeHandlerResult(
  metadata: ZLinkNestHandlerMetadata,
  result: unknown,
  context: ZLinkRequestContext | ZLinkRouteRequestContext
): unknown {
  return metadata.encodeResult === undefined
    ? result
    : metadata.encodeResult(result, context);
}

function decodePayload(
  metadata: ZLinkNestHandlerMetadata | undefined,
  payload: Buffer | Uint8Array | string | unknown,
  context: ZLinkRequestContext | ZLinkSendContext | ZLinkRouteRequestContext | ZLinkRouteSendContext | ZLinkPublishContext
): unknown {
  if (Buffer.isBuffer(payload) || payload instanceof Uint8Array) {
    if (metadata?.decodePayload !== undefined) {
      return metadata.decodePayload(Buffer.from(payload), context);
    }
    if (context.contentType !== undefined && context.contentType !== 'application/json') {
      return Buffer.from(payload);
    }
    return parseWireJson(Buffer.from(payload).toString());
  }
  if (typeof payload === 'string') {
    return parseWireJson(payload);
  }
  return payload;
}

function parseWireJson(payload: string): unknown {
  return JSON.parse(payload, (key, value) => {
    if (isPrototypeKey(key)) {
      throw new framework.ZLinkConfigurationException(`NestJS handler JSON key '${key}' is not allowed.`);
    }
    return value;
  });
}

function isPrototypeKey(key: string): boolean {
  return key === '__proto__' || key === 'constructor' || key === 'prototype';
}

function hasNestHandlerDiscovery(options: ZLinkNestModuleRegistrationOptions): boolean {
  return hasConfiguredSpotNodes(options.spotNodes)
    || Object.values(options.clientServerChannels ?? {}).some(
      (channel) => (channel.handlerGroups ?? []).length > 0
        || (channel.requestHandlerTypes ?? []).length > 0
        || (channel.sendHandlerTypes ?? []).length > 0
    )
    || Object.values(options.fanoutChannels ?? {}).some(
      (channel) => (channel.handlerGroups ?? []).length > 0
        || (channel.publishHandlerTypes ?? []).length > 0
    )
    || Object.values(options.routerMeshes ?? {}).some(
      (channel) => (channel.handlerGroups ?? []).length > 0
        || (channel.requestHandlerTypes ?? []).length > 0
        || (channel.sendHandlerTypes ?? []).length > 0
    );
}

function hasConfiguredSpotNodes(value: ZLinkNestModuleRegistrationOptions['spotNodes']): boolean {
  if (value === undefined) {
    return false;
  }
  return Array.isArray(value) ? value.length > 0 : Object.keys(value).length > 0;
}

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
    requiresRuntime: true,
    isEnabled: (registration) => framework.hasSpotNode(registration),
    create: (registration, runtime, moduleRef, discovery) =>
      createSpotManager(registration, requireRuntime(runtime), moduleRef, discovery)
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
    requiresRuntime: true,
    isEnabled: (registration) => framework.hasActorManager(registration),
    create: async (registration, runtime, moduleRef, discovery) => {
      const host = requireRuntime(runtime);
      const remoteAddressResolver = framework.hasSpotRemoteAddressResolver(registration)
        ? await createSpotRemoteAddressResolver(registration, moduleRef, discovery, host)
        : undefined;
      const hostActorOptions = host.createActorManagerOptions?.(remoteAddressResolver) as Record<string, unknown> | undefined;
      const actorManager = new framework.DefaultZLinkActorManager({
        actorFactories: registration.actorFactories,
        ...hostActorOptions,
        boundSessionFactory: hostActorOptions?.boundSessionFactory ?? host.boundSessionFactory.create.bind(host.boundSessionFactory),
        providerResolver: moduleRef === undefined ? undefined : createProviderResolver(moduleRef, discovery)
      });
      host.setActorManager?.(actorManager);
      return actorManager;
    }
  }
];

function conditionalClientProvidersForFactory(): Provider[] {
  return [
    ...CONDITIONAL_CLIENT_PROVIDER_SPECS.map(createConditionalClientProviderForFactory),
    {
      provide: ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER,
      inject: [ZLINK_FRAMEWORK_REGISTRATION, ModuleRef, DiscoveryService, ZLINK_FRAMEWORK_RUNTIME],
      useFactory: async (
        registration: ZLinkFrameworkRegistration,
        moduleRef: ModuleRef,
        discovery: DiscoveryService,
        runtime: FrameworkRuntimeHost
      ) => {
        if (!framework.hasSpotRemoteAddressResolver(registration)) {
          return null;
        }
        return await createSpotRemoteAddressResolver(registration, moduleRef, discovery, runtime);
      }
    }
  ];
}

function createConditionalClientProviderForFactory(spec: ConditionalClientProviderSpec): Provider {
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
  runtime: FrameworkRuntimeHost,
  moduleRef: ModuleRef | undefined,
  discovery: DiscoveryService | undefined
): unknown {
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [...registration.spotFactories],
    spotTimerHandlers: [...registration.spotNodes.values()]
      .flatMap((spotNode) => [...(spotNode.spotTimerHandlers ?? [])]),
    spotActorSendHandlers: [...registration.spotNodes.values()]
      .flatMap((spotNode) => [...(spotNode.spotActorSendHandlers ?? [])]),
    spotActorRequestHandlers: [...registration.spotNodes.values()]
      .flatMap((spotNode) => [...(spotNode.spotActorRequestHandlers ?? [])]),
    ...runtime.createSpotManagerOptions?.(),
    providerResolver: moduleRef === undefined ? undefined : createProviderResolver(moduleRef, discovery),
    workerRuntime: new framework.ZLinkSpotWorkerRuntime(registration.worker)
  });
  runtime.setSpotManager?.(manager);
  return manager;
}

async function createSpotOutbound(
  registration: ZLinkFrameworkRegistration,
  runtime: FrameworkRuntimeHost,
  moduleRef: ModuleRef | undefined,
  discovery: DiscoveryService | undefined
): Promise<unknown> {
  const resolver = framework.hasSpotRemoteAddressResolver(registration)
    ? await createSpotRemoteAddressResolver(registration, moduleRef, discovery, runtime)
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
  discovery?: DiscoveryService,
  runtime?: FrameworkRuntimeHost
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
    const resolver = runtime?.createRegistrySpotRemoteAddressResolver?.();
    if (resolver === undefined) {
      throw new framework.ZLinkConfigurationException('Registry SPOT remote address resolver requires framework runtime.');
    }
    return resolver;
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
    inject: [ModuleRef, DiscoveryService, ZLINK_FRAMEWORK_RUNTIME],
    useFactory: (moduleRef: ModuleRef, discovery: DiscoveryService, runtime: FrameworkRuntimeHost) =>
      createSpotRemoteAddressResolver(registration, moduleRef, discovery, runtime)
  }];
}

function loadFramework(): FrameworkModule {
  const requireFramework = createRequire(__filename);
  return requireFramework(path.resolve(__dirname, '../../framework/dist/internal')) as FrameworkModule;
}
