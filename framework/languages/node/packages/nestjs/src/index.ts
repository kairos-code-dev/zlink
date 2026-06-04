import 'reflect-metadata';
import { createRequire } from 'node:module';
import path from 'node:path';
import { Module } from '@nestjs/common';
import type { DynamicModule, InjectionToken, ModuleMetadata, OnModuleDestroy, OnModuleInit, Provider } from '@nestjs/common';
import { DiscoveryModule, DiscoveryService, ModuleRef } from '@nestjs/core';
import type {
  Type,
  ZLinkChannelOptions,
  ZLinkDecoratorMetadata,
  ZLinkFrameworkRegistration,
  ZLinkFrameworkRegistrationOptions,
  ZLinkPublishContext,
  ZLinkRequestContext,
  ZLinkRouteSendContext,
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
      imports: options.imports,
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

function createDiscoveredOptions(
  options: ZLinkModuleOptions,
  discovery: DiscoveryService,
  moduleRef: ModuleRef
): ZLinkFrameworkRegistrationOptions {
  const channels: Record<string, ZLinkChannelOptions> = {};
  const providerRefs = discoverProviderRefs(discovery);

  for (const [channelName, channel] of Object.entries(options.channels ?? {})) {
    const { handlerGroups, handlerTypes, ...baseChannel } = channel;
    const requestHandlers = createDiscoveredRequestHandlers(providerRefs, handlerGroups, handlerTypes, moduleRef);
    const sendHandlers = createDiscoveredSendHandlers(providerRefs, handlerGroups, handlerTypes, moduleRef);
    const publishHandlers = createDiscoveredPublishHandlers(providerRefs, handlerGroups, handlerTypes, moduleRef);
    const routeMesh = baseChannel.routeMesh === undefined
      ? undefined
      : {
          ...baseChannel.routeMesh,
          requestHandlers: [
            ...(baseChannel.routeMesh.requestHandlers ?? []),
            ...requestHandlers
          ],
          sendHandlers: [
            ...(baseChannel.routeMesh.sendHandlers ?? []),
            ...sendHandlers
          ]
        };
    const channelRequestHandlers = baseChannel.server === undefined
      ? baseChannel.requestHandlers
      : [
          ...(baseChannel.requestHandlers ?? []),
          ...requestHandlers
        ];

    channels[channelName] = {
      ...baseChannel,
      routeMesh,
      publishHandlers: [
        ...(baseChannel.publishHandlers ?? []),
        ...publishHandlers
      ],
      requestHandlers: channelRequestHandlers
    };
  }

  return {
    ...options,
    channels
  };
}

interface DiscoveredNestProvider {
  readonly handlerType: Type;
  readonly token: InjectionToken;
  readonly instance?: Record<string, unknown>;
}

function createDiscoveredRequestHandlers(
  providerRefs: readonly DiscoveredNestProvider[],
  handlerGroups: readonly string[] | undefined,
  explicitHandlerTypes: readonly Type[] | undefined,
  moduleRef: ModuleRef
): NonNullable<ZLinkChannelOptions['requestHandlers']> {
  return createDiscoveredHandlerRegistrations(providerRefs, handlerGroups, explicitHandlerTypes, 'request', (ref, metadata) => ({
    async handle(payload: Buffer, context: ZLinkRequestContext) {
      return await invokeDiscoveredHandler(moduleRef, ref, metadata, payload, context);
    }
  }));
}

function createDiscoveredSendHandlers(
  providerRefs: readonly DiscoveredNestProvider[],
  handlerGroups: readonly string[] | undefined,
  explicitHandlerTypes: readonly Type[] | undefined,
  moduleRef: ModuleRef
): NonNullable<NonNullable<ZLinkChannelOptions['routeMesh']>['sendHandlers']> {
  return createDiscoveredHandlerRegistrations(providerRefs, handlerGroups, explicitHandlerTypes, 'send', (ref, metadata) => ({
    async handle(payload: Buffer, context: ZLinkRouteSendContext) {
      await invokeDiscoveredHandler(moduleRef, ref, metadata, payload, context);
    }
  }));
}

function createDiscoveredPublishHandlers(
  providerRefs: readonly DiscoveredNestProvider[],
  handlerGroups: readonly string[] | undefined,
  explicitHandlerTypes: readonly Type[] | undefined,
  moduleRef: ModuleRef
): NonNullable<ZLinkChannelOptions['publishHandlers']> {
  return createDiscoveredHandlerRegistrations(providerRefs, handlerGroups, explicitHandlerTypes, 'publish', (ref, metadata) => ({
    async handle(payload: Buffer, context: ZLinkPublishContext) {
      await invokeDiscoveredHandler(moduleRef, ref, metadata, payload, context);
    }
  }));
}

function createDiscoveredHandlerRegistrations<THandler>(
  providerRefs: readonly DiscoveredNestProvider[],
  handlerGroups: readonly string[] | undefined,
  explicitHandlerTypes: readonly Type[] | undefined,
  kind: string,
  createHandler: (ref: DiscoveredNestProvider, metadata: ZLinkDecoratorMetadata) => THandler
): Array<{ readonly packetName: string; readonly handler: THandler }> {
  const descriptors = createDiscoveredHandlerDescriptors(providerRefs, handlerGroups, explicitHandlerTypes, kind);

  return descriptors.map(({ ref, metadata }) => ({
    packetName: metadata.packetName ?? ref.handlerType.name,
    handler: createHandler(ref, metadata)
  }));
}

function createDiscoveredHandlerDescriptors(
  providerRefs: readonly DiscoveredNestProvider[],
  handlerGroups: readonly string[] | undefined,
  explicitHandlerTypes: readonly Type[] | undefined,
  kind: string
): Array<{ readonly ref: DiscoveredNestProvider; readonly metadata: ZLinkDecoratorMetadata }> {
  if ((handlerGroups ?? []).length === 0 && (explicitHandlerTypes ?? []).length === 0) {
    return [];
  }

  const refs = mergeExplicitProviderRefs(providerRefs, explicitHandlerTypes);
  const refByType = new Map<Type, DiscoveredNestProvider>();
  for (const ref of refs) {
    if (!refByType.has(ref.handlerType)) {
      refByType.set(ref.handlerType, ref);
    }
  }

  const descriptors = framework.exposeZLinkHandlers([...refByType.keys()], {
    handlerGroups,
    explicitHandlers: explicitHandlerTypes
  }).filter((descriptor) => descriptor.metadata.kind === kind);

  const seen = new Set<string>();
  const selected: Array<{ readonly ref: DiscoveredNestProvider; readonly metadata: ZLinkDecoratorMetadata }> = [];
  for (const descriptor of descriptors) {
    const ref = refByType.get(descriptor.handlerType);
    if (ref === undefined) {
      continue;
    }
    const packetName = descriptor.metadata.packetName ?? descriptor.handlerType.name;
    const key = `${descriptor.metadata.kind}:${packetName}`;
    if (seen.has(key)) {
      throw new framework.ZLinkConfigurationException(
        `Duplicate discovered handler '${descriptor.metadata.kind}:${packetName}'.`
      );
    }
    seen.add(key);
    selected.push({ ref, metadata: descriptor.metadata });
  }
  return selected;
}

function mergeExplicitProviderRefs(
  providerRefs: readonly DiscoveredNestProvider[],
  explicitHandlerTypes: readonly Type[] | undefined
): DiscoveredNestProvider[] {
  const refs = [...providerRefs];
  for (const handlerType of explicitHandlerTypes ?? []) {
    if (!refs.some((ref) => ref.handlerType === handlerType)) {
      refs.push({ handlerType, token: handlerType });
    }
  }
  return refs;
}

function discoverProviderRefs(discovery: DiscoveryService): DiscoveredNestProvider[] {
  const refs: DiscoveredNestProvider[] = [];
  const seen = new Set<string>();

  for (const wrapper of discovery.getProviders()) {
    const token = wrapper.token as InjectionToken | undefined;
    if (token === undefined) {
      continue;
    }

    const candidates = [wrapper.metatype, wrapper.instance?.constructor, token]
      .filter((value): value is Type => typeof value === 'function');
    for (const handlerType of new Set(candidates)) {
      if (framework.readZLinkDecoratorMetadata(handlerType).length === 0) {
        continue;
      }
      const key = `${String(token)}:${handlerType.name}`;
      if (seen.has(key)) {
        continue;
      }
      seen.add(key);
      refs.push({
        handlerType,
        token,
        instance: wrapper.instance === undefined ? undefined : wrapper.instance as Record<string, unknown>
      });
    }
  }

  return refs;
}

async function invokeDiscoveredHandler(
  moduleRef: ModuleRef,
  ref: DiscoveredNestProvider,
  metadata: ZLinkDecoratorMetadata,
  payload: Buffer,
  context: ZLinkRequestContext | ZLinkRouteSendContext | ZLinkPublishContext
): Promise<unknown> {
  const instance = ref.instance ?? moduleRef.get(ref.token, { strict: false }) as Record<string, unknown>;
  const methodName = metadata.methodName ?? 'handle';
  const method = instance[methodName];
  if (typeof method !== 'function') {
    throw new framework.ZLinkConfigurationException(
      `Discovered handler ${ref.handlerType.name}.${methodName} is not callable.`
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
  create(registration: ZLinkFrameworkRegistration, runtime: FrameworkRuntimeHost | undefined): unknown;
}

const CONDITIONAL_CLIENT_PROVIDER_SPECS: readonly ConditionalClientProviderSpec[] = [
  {
    token: ZLINK_SPOT_MANAGER,
    requiresRuntime: false,
    isEnabled: (registration) => framework.hasSpotNode(registration),
    create: (registration) => createSpotManager(registration)
  },
  {
    token: ZLINK_SPOT_OUTBOUND,
    requiresRuntime: true,
    isEnabled: (registration) => framework.hasSpotNode(registration),
    create: (registration, runtime) => createSpotOutbound(registration, requireRuntime(runtime))
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
    create: (registration) => new framework.DefaultZLinkActorManager({ actorFactories: registration.actorFactories })
  }
];

function conditionalClientProvidersForAsync(): Provider[] {
  return [
    ...CONDITIONAL_CLIENT_PROVIDER_SPECS.map(createConditionalClientProviderForAsync),
    {
      provide: ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER,
      inject: [ZLINK_FRAMEWORK_REGISTRATION],
      useFactory: (registration: ZLinkFrameworkRegistration) => {
        if (!framework.hasSpotRemoteAddressResolver(registration)) {
          return null;
        }
        return createSpotRemoteAddressResolver(registration);
      }
    }
  ];
}

function createConditionalClientProviderForAsync(spec: ConditionalClientProviderSpec): Provider {
  return {
    provide: spec.token,
    inject: spec.requiresRuntime
      ? [ZLINK_FRAMEWORK_REGISTRATION, ZLINK_FRAMEWORK_RUNTIME]
      : [ZLINK_FRAMEWORK_REGISTRATION],
    useFactory: (registration: ZLinkFrameworkRegistration, runtime?: FrameworkRuntimeHost) => {
      if (!spec.isEnabled(registration)) {
        return null;
      }
      return spec.create(registration, runtime);
    }
  };
}

function createConditionalClientProvider(
  spec: ConditionalClientProviderSpec,
  registration: ZLinkFrameworkRegistration
): Provider {
  if (!spec.requiresRuntime) {
    return { provide: spec.token, useValue: spec.create(registration, undefined) };
  }
  return {
    provide: spec.token,
    inject: [ZLINK_FRAMEWORK_RUNTIME],
    useFactory: (runtime: FrameworkRuntimeHost) => spec.create(registration, runtime)
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
