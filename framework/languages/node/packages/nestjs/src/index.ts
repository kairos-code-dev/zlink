import 'reflect-metadata';
import { createRequire } from 'node:module';
import path from 'node:path';
import { Module } from '@nestjs/common';
import type { DynamicModule, InjectionToken, OnModuleDestroy, OnModuleInit, Provider } from '@nestjs/common';
import { DiscoveryModule, DiscoveryService, ModuleRef } from '@nestjs/core';
import type {
  Type,
  ZLinkChannelOptions,
  ZLinkDecoratorMetadata,
  ZLinkFrameworkRegistration,
  ZLinkFrameworkRegistrationOptions,
  ZLinkRequestContext,
  ZLinkRegistryOptions,
  ZLinkRegistryQueryClientOptions,
  ZLinkSpotRemoteAddressResolver
} from '@zlink-systems/framework';

type FrameworkModule = typeof import('@zlink-systems/framework');

const framework = loadFramework();

type RuntimeHost = InstanceType<FrameworkModule['ZLinkFrameworkRuntimeHost']>;
type RuntimeHostWithNestLifecycle = RuntimeHost & OnModuleInit & OnModuleDestroy;

export interface ZLinkModuleAsyncOptions {
  readonly useFactory: (...args: unknown[]) => ZLinkModuleOptions | Promise<ZLinkModuleOptions>;
  readonly inject?: readonly InjectionToken[];
}

export interface ZLinkNestChannelOptions extends ZLinkChannelOptions {
  readonly handlerGroups?: readonly string[];
  readonly handlerTypes?: readonly Type[];
}

export interface ZLinkModuleOptions extends Omit<ZLinkFrameworkRegistrationOptions, 'channels'> {
  readonly channels?: Readonly<Record<string, ZLinkNestChannelOptions>>;
}

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

@Module({})
export class ZLinkModule {
  static forRoot(options: ZLinkModuleOptions = {}): DynamicModule {
    if (hasNestHandlerDiscovery(options)) {
      return createDiscoveringZLinkDynamicModule(options);
    }
    return createZLinkDynamicModule(framework.createFrameworkRegistration(options));
  }

  static forRootAsync(options: ZLinkModuleAsyncOptions): DynamicModule {
    const registrationProvider: Provider<Promise<ZLinkFrameworkRegistration>> = {
      provide: ZLINK_FRAMEWORK_REGISTRATION,
      inject: options.inject === undefined ? undefined : [...options.inject],
      useFactory: async (...args: unknown[]) => framework.createFrameworkRegistration(await options.useFactory(...args))
    };

    return {
      module: ZLinkModule,
      providers: [
        registrationProvider,
        {
          provide: ZLINK_FRAMEWORK_RUNTIME,
          inject: [ZLINK_FRAMEWORK_REGISTRATION],
          useFactory: (registration: ZLinkFrameworkRegistration) => createRuntimeHost(registration)
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
}

export function createZLinkDynamicModule(registration: ZLinkFrameworkRegistration): DynamicModule {
  const providers: Provider[] = [
    { provide: ZLINK_FRAMEWORK_REGISTRATION, useValue: registration },
    { provide: ZLINK_FRAMEWORK_RUNTIME, useFactory: () => createRuntimeHost(registration) },
    ...alwaysAvailableClientProviders(registration),
    ...conditionalClientProviders(registration)
  ];

  return {
    module: ZLinkModule,
    providers,
    exports: providers.map(providerToken)
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
        inject: [ZLINK_FRAMEWORK_REGISTRATION],
        useFactory: (registration: ZLinkFrameworkRegistration) => createRuntimeHost(registration)
      },
      ...alwaysAvailableClientProviders()
    ],
    exports: [
      ZLINK_FRAMEWORK_RUNTIME,
      ...alwaysAvailableClientTokens()
    ]
  };
}

function createDiscoveredOptions(
  options: ZLinkModuleOptions,
  discovery: DiscoveryService,
  moduleRef: ModuleRef
): ZLinkFrameworkRegistrationOptions {
  const channels: Record<string, ZLinkChannelOptions> = {};
  const providerTypes = discoverProviderTypes(discovery);

  for (const [channelName, channel] of Object.entries(options.channels ?? {})) {
    const { handlerGroups, handlerTypes, ...baseChannel } = channel;
    const requestHandlers = [
      ...(baseChannel.requestHandlers ?? []),
      ...createDiscoveredRequestHandlers(providerTypes, handlerGroups, handlerTypes, moduleRef)
    ];
    channels[channelName] = requestHandlers.length === 0
      ? baseChannel
      : { ...baseChannel, requestHandlers };
  }

  return {
    ...options,
    channels
  };
}

function createDiscoveredRequestHandlers(
  providerTypes: readonly Type[],
  handlerGroups: readonly string[] | undefined,
  explicitHandlerTypes: readonly Type[] | undefined,
  moduleRef: ModuleRef
): NonNullable<ZLinkChannelOptions['requestHandlers']> {
  if ((handlerGroups ?? []).length === 0 && (explicitHandlerTypes ?? []).length === 0) {
    return [];
  }

  const knownTypes = [...new Set([...providerTypes, ...(explicitHandlerTypes ?? [])])];
  const descriptors = framework.exposeZLinkHandlers(knownTypes, {
    handlerGroups,
    explicitHandlers: explicitHandlerTypes
  }).filter((descriptor) => descriptor.metadata.kind === 'request');

  return descriptors.map(({ handlerType, metadata }) => ({
    packetName: metadata.packetName ?? handlerType.name,
    handler: {
      async handle(payload: Buffer, context: ZLinkRequestContext) {
        return await invokeDiscoveredHandler(moduleRef, handlerType, metadata, payload, context);
      }
    }
  }));
}

function discoverProviderTypes(discovery: DiscoveryService): Type[] {
  return discovery.getProviders()
    .flatMap((wrapper) => [wrapper.token, wrapper.metatype, wrapper.instance?.constructor])
    .filter((value): value is Type => typeof value === 'function');
}

async function invokeDiscoveredHandler(
  moduleRef: ModuleRef,
  handlerType: Type,
  metadata: ZLinkDecoratorMetadata,
  payload: Buffer,
  context: ZLinkRequestContext
): Promise<unknown> {
  const instance = moduleRef.get(handlerType, { strict: false }) as Record<string, unknown>;
  const methodName = metadata.methodName ?? 'handle';
  const method = instance[methodName];
  if (typeof method !== 'function') {
    throw new framework.ZLinkConfigurationException(
      `Discovered handler ${handlerType.name}.${methodName} is not callable.`
    );
  }
  return await method.call(instance, decodePayload(payload), context);
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
  return Object.values(options.channels ?? {}).some(
    (channel) => (channel.handlerGroups ?? []).length > 0 || (channel.handlerTypes ?? []).length > 0
  );
}

function alwaysAvailableClientProviders(registration?: ZLinkFrameworkRegistration): Provider[] {
  if (registration === undefined) {
    return [
      {
        provide: ZLINK_CHANNEL_CLIENT,
        inject: [ZLINK_FRAMEWORK_REGISTRATION, ZLINK_FRAMEWORK_RUNTIME],
        useFactory: (
          resolved: ZLinkFrameworkRegistration,
          runtime: InstanceType<FrameworkModule['ZLinkFrameworkRuntimeHost']>
        ) => new framework.DefaultZLinkChannelClient(resolved, runtime.channelTransport)
      },
      {
        provide: ZLINK_FANOUT_CLIENT,
        inject: [ZLINK_FRAMEWORK_REGISTRATION, ZLINK_FRAMEWORK_RUNTIME],
        useFactory: (
          resolved: ZLinkFrameworkRegistration,
          runtime: InstanceType<FrameworkModule['ZLinkFrameworkRuntimeHost']>
        ) => new framework.DefaultZLinkFanoutClient(resolved, runtime.channelTransport)
      },
      {
        provide: ZLINK_ROUTE_CLIENT,
        inject: [ZLINK_FRAMEWORK_REGISTRATION, ZLINK_FRAMEWORK_RUNTIME],
        useFactory: (
          resolved: ZLinkFrameworkRegistration,
          runtime: InstanceType<FrameworkModule['ZLinkFrameworkRuntimeHost']>
        ) => new framework.DefaultZLinkRouteClient(resolved, runtime.routeTransport)
      },
      {
        provide: ZLINK_BOUND_SESSION_FACTORY,
        inject: [ZLINK_FRAMEWORK_RUNTIME],
        useFactory: (runtime: InstanceType<FrameworkModule['ZLinkFrameworkRuntimeHost']>) => runtime.boundSessionFactory
      },
      { provide: ZLINK_MESSAGE_METADATA_POLICY, useValue: Object.freeze({ forward: true }) }
    ];
  }

  return [
    {
      provide: ZLINK_CHANNEL_CLIENT,
      inject: [ZLINK_FRAMEWORK_RUNTIME],
      useFactory: (runtime: InstanceType<FrameworkModule['ZLinkFrameworkRuntimeHost']>) =>
        new framework.DefaultZLinkChannelClient(registration, runtime.channelTransport)
    },
    {
      provide: ZLINK_FANOUT_CLIENT,
      inject: [ZLINK_FRAMEWORK_RUNTIME],
      useFactory: (runtime: InstanceType<FrameworkModule['ZLinkFrameworkRuntimeHost']>) =>
        new framework.DefaultZLinkFanoutClient(registration, runtime.channelTransport)
    },
    {
      provide: ZLINK_ROUTE_CLIENT,
      inject: [ZLINK_FRAMEWORK_RUNTIME],
      useFactory: (runtime: InstanceType<FrameworkModule['ZLinkFrameworkRuntimeHost']>) =>
        new framework.DefaultZLinkRouteClient(registration, runtime.routeTransport)
    },
    {
      provide: ZLINK_BOUND_SESSION_FACTORY,
      inject: [ZLINK_FRAMEWORK_RUNTIME],
      useFactory: (runtime: InstanceType<FrameworkModule['ZLinkFrameworkRuntimeHost']>) => runtime.boundSessionFactory
    },
    { provide: ZLINK_MESSAGE_METADATA_POLICY, useValue: Object.freeze({ forward: true }) }
  ];
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
  const providers: Provider[] = [];

  if (framework.hasSpotNode(registration)) {
    providers.push(
      { provide: ZLINK_SPOT_MANAGER, useValue: createSpotManager(registration) },
      {
        provide: ZLINK_SPOT_OUTBOUND,
        inject: [ZLINK_FRAMEWORK_RUNTIME],
        useFactory: (runtime: InstanceType<FrameworkModule['ZLinkFrameworkRuntimeHost']>) =>
          createSpotOutbound(registration, runtime)
      }
    );
  }

  if (framework.hasSpotPublisherClient(registration)) {
    providers.push({
      provide: ZLINK_SPOT_PUBLISHER_CLIENT,
      inject: [ZLINK_FRAMEWORK_RUNTIME],
      useFactory: (runtime: InstanceType<FrameworkModule['ZLinkFrameworkRuntimeHost']>) =>
        new framework.DefaultZLinkSpotPublisherClient(registration, runtime.spotPublisherTransport)
    });
  }

  if (framework.hasActorManager(registration)) {
    providers.push({
      provide: ZLINK_ACTOR_MANAGER,
      useValue: new framework.DefaultZLinkActorManager({ actorFactories: registration.actorFactories })
    });
  }

  if (framework.hasSpotRemoteAddressResolver(registration)) {
    providers.push(...spotRemoteAddressResolverProviders(registration));
  }

  return providers;
}

function conditionalClientProvidersForAsync(): Provider[] {
  return [
    {
      provide: ZLINK_SPOT_MANAGER,
      inject: [ZLINK_FRAMEWORK_REGISTRATION],
      useFactory: (registration: ZLinkFrameworkRegistration) => {
        ensureCapability(framework.hasSpotNode(registration), ZLINK_SPOT_MANAGER);
        return createSpotManager(registration);
      }
    },
    {
      provide: ZLINK_SPOT_OUTBOUND,
      inject: [ZLINK_FRAMEWORK_REGISTRATION, ZLINK_FRAMEWORK_RUNTIME],
      useFactory: (
        registration: ZLinkFrameworkRegistration,
        runtime: InstanceType<FrameworkModule['ZLinkFrameworkRuntimeHost']>
      ) => {
        ensureCapability(framework.hasSpotNode(registration), ZLINK_SPOT_OUTBOUND);
        return createSpotOutbound(registration, runtime);
      }
    },
    {
      provide: ZLINK_SPOT_PUBLISHER_CLIENT,
      inject: [ZLINK_FRAMEWORK_REGISTRATION, ZLINK_FRAMEWORK_RUNTIME],
      useFactory: (
        registration: ZLinkFrameworkRegistration,
        runtime: InstanceType<FrameworkModule['ZLinkFrameworkRuntimeHost']>
      ) => {
        ensureCapability(framework.hasSpotPublisherClient(registration), ZLINK_SPOT_PUBLISHER_CLIENT);
        return new framework.DefaultZLinkSpotPublisherClient(registration, runtime.spotPublisherTransport);
      }
    },
    {
      provide: ZLINK_ACTOR_MANAGER,
      inject: [ZLINK_FRAMEWORK_REGISTRATION],
      useFactory: (registration: ZLinkFrameworkRegistration) => {
        ensureCapability(framework.hasActorManager(registration), ZLINK_ACTOR_MANAGER);
        return new framework.DefaultZLinkActorManager({ actorFactories: registration.actorFactories });
      }
    },
    {
      provide: ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER,
      inject: [ZLINK_FRAMEWORK_REGISTRATION],
      useFactory: (registration: ZLinkFrameworkRegistration) => {
        ensureCapability(framework.hasSpotRemoteAddressResolver(registration), ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER);
        return createSpotRemoteAddressResolver(registration);
      }
    }
  ];
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

function createRuntimeHost(registration: ZLinkFrameworkRegistration): RuntimeHostWithNestLifecycle {
  const runtime = new framework.ZLinkFrameworkRuntimeHost({ registration }) as RuntimeHostWithNestLifecycle;
  runtime.onModuleInit = async () => {
    await runtime.start();
  };
  runtime.onModuleDestroy = async () => {
    await runtime.stop();
  };
  return runtime;
}

function providerToken(provider: Provider): InjectionToken {
  return typeof provider === 'function' ? provider : provider.provide;
}

function ensureCapability(enabled: boolean, token: InjectionToken): void {
  if (!enabled) {
    throw new framework.ZLinkConfigurationException(`Provider ${String(token)} requires a matching ZLink capability.`);
  }
}

function createSpotManager(registration: ZLinkFrameworkRegistration): InstanceType<FrameworkModule['DefaultZLinkSpotManager']> {
  return new framework.DefaultZLinkSpotManager({ spotFactories: [...registration.spotFactories] });
}

function createSpotOutbound(
  registration: ZLinkFrameworkRegistration,
  runtime: InstanceType<FrameworkModule['ZLinkFrameworkRuntimeHost']>
): InstanceType<FrameworkModule['DefaultZLinkSpotOutbound']> {
  const resolver = framework.hasSpotRemoteAddressResolver(registration)
    ? createSpotRemoteAddressResolver(registration)
    : undefined;
  return new framework.DefaultZLinkSpotOutbound(
    new framework.ZLinkSpotSerialExecutor(),
    undefined,
    undefined,
    resolver,
    runtime.routeTransport
  );
}

function createSpotRemoteAddressResolver(
  registration: ZLinkFrameworkRegistration
): ZLinkSpotRemoteAddressResolver {
  if (registration.spotRemoteAddressResolverType !== undefined) {
    return new (registration.spotRemoteAddressResolverType as new () => ZLinkSpotRemoteAddressResolver)();
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
    useFactory: () => createSpotRemoteAddressResolver(registration)
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
