import type { InjectionToken, OnModuleDestroy, OnModuleInit, Provider } from '@nestjs/common';
import { DiscoveryService, ModuleRef } from '@nestjs/core';
import type {
  Type,
  ZLinkFrameworkRegistration,
  ZLinkProviderResolver,
  ZLinkRuntimeEvent,
  ZLinkRuntimeEventHandler,
  ZLinkRuntimeEventPublisher
} from '@zlink-systems/framework';
import {
  ZLINK_ACTOR_CLIENT,
  ZLINK_ACTOR_MANAGER,
  ZLINK_ACTOR_SPOT_REF_RESOLVER,
  ZLINK_BOUND_SESSION_FACTORY,
  ZLINK_CHANNEL_CLIENT,
  ZLINK_CHANNEL_RUNTIME_OPTIONS,
  ZLINK_FANOUT_CLIENT,
  ZLINK_FRAMEWORK_REGISTRATION,
  ZLINK_FRAMEWORK_RUNTIME,
  ZLINK_LOCATION_RUNTIME_QUERY,
  ZLINK_MESSAGE_METADATA_POLICY,
  ZLINK_ROUTE_CLIENT,
  ZLINK_RUNTIME_EVENT_PUBLISHER,
  ZLINK_SPOT_MANAGER,
  ZLINK_SPOT_OUTBOUND,
  ZLINK_SPOT_PUBLISHER_CLIENT,
  ZLINK_SPOT_REF_RESOLVER
} from './tokens';
import {
  claimRuntimeEventHandler,
  isNestRuntimeEventHandler
} from './handler-metadata';
import {
  loadFramework,
  type FrameworkRuntimeHost
} from './framework-loader';

const framework = loadFramework();

type RuntimeHostWithNestLifecycle = FrameworkRuntimeHost & OnModuleInit & OnModuleDestroy;

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
    token: ZLINK_CHANNEL_RUNTIME_OPTIONS,
    create: (_registration, runtime) => runtime.channelRuntimeOptions
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
  },
  {
    token: ZLINK_RUNTIME_EVENT_PUBLISHER,
    create: (_registration, runtime) => runtime.eventPublisher
  }
];

export function alwaysAvailableClientProviders(registration?: ZLinkFrameworkRegistration): Provider[] {
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

export function alwaysAvailableClientTokens(): InjectionToken[] {
  return [
    ZLINK_CHANNEL_CLIENT,
    ZLINK_ROUTE_CLIENT,
    ZLINK_FANOUT_CLIENT,
    ZLINK_BOUND_SESSION_FACTORY,
    ZLINK_RUNTIME_EVENT_PUBLISHER,
    ZLINK_MESSAGE_METADATA_POLICY
  ];
}

export function conditionalClientProviders(registration: ZLinkFrameworkRegistration): Provider[] {
  return CONDITIONAL_CLIENT_PROVIDER_SPECS
    .filter((spec) => spec.isEnabled(registration))
    .map((spec) => createConditionalClientProvider(spec, registration));
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
    token: ZLINK_ACTOR_CLIENT,
    requiresRuntime: true,
    isEnabled: (registration) => framework.hasSpotNode(registration) && hasLocationStores(registration),
    create: (_registration, runtime) =>
      new framework.DefaultZLinkActorClient(requireRuntime(runtime).createActorClientOptions?.() ?? {})
  },
  {
    token: ZLINK_ACTOR_MANAGER,
    requiresRuntime: true,
    isEnabled: (registration) => framework.hasActorManager(registration),
    create: async (registration, runtime, moduleRef, discovery) => {
      const host = requireRuntime(runtime);
      const hostActorOptions = host.createActorManagerOptions?.() as Record<string, unknown> | undefined;
      const actorManager = new framework.DefaultZLinkActorManager({
        actorFactories: registration.actorFactories,
        ...hostActorOptions,
        boundSessionFactory: hostActorOptions?.boundSessionFactory ?? host.boundSessionFactory.create.bind(host.boundSessionFactory),
        providerResolver: moduleRef === undefined ? undefined : createProviderResolver(moduleRef, discovery)
      });
      host.setActorManager?.(actorManager);
      return actorManager;
    }
  },
  {
    token: ZLINK_LOCATION_RUNTIME_QUERY,
    requiresRuntime: true,
    isEnabled: (registration) => hasLocationStores(registration),
    create: (_registration, runtime) => {
      const query = requireRuntime(runtime).locationRuntimeQuery;
      if (query === undefined) {
        throw new framework.ZLinkConfigurationException('Location runtime query requires location stores.');
      }
      return query;
    }
  },
  {
    token: ZLINK_SPOT_REF_RESOLVER,
    requiresRuntime: true,
    isEnabled: (registration) => hasLocationStores(registration),
    create: (_registration, runtime) => {
      const resolver = requireRuntime(runtime).createLocationRefResolver?.();
      if (resolver === undefined) {
        throw new framework.ZLinkConfigurationException('SpotRef resolver requires location stores.');
      }
      return resolver;
    }
  },
  {
    token: ZLINK_ACTOR_SPOT_REF_RESOLVER,
    requiresRuntime: true,
    isEnabled: (registration) => hasLocationStores(registration),
    create: (_registration, runtime) => {
      const resolver = requireRuntime(runtime).createLocationRefResolver?.();
      if (resolver === undefined) {
        throw new framework.ZLinkConfigurationException('Actor SpotRef resolver requires location stores.');
      }
      return resolver;
    }
  }
];

export function conditionalClientProvidersForFactory(): Provider[] {
  return CONDITIONAL_CLIENT_PROVIDER_SPECS.map(createConditionalClientProviderForFactory);
}

function createConditionalClientProviderForFactory(spec: ConditionalClientProviderSpec): Provider {
  return createConditionalClientProviderFromSpec(spec, { checkEnabled: true });
}

function createConditionalClientProvider(
  spec: ConditionalClientProviderSpec,
  registration: ZLinkFrameworkRegistration
): Provider {
  return createConditionalClientProviderFromSpec(spec, { registration, checkEnabled: false });
}

interface ConditionalClientProviderOptions {
  readonly registration?: ZLinkFrameworkRegistration;
  readonly checkEnabled: boolean;
}

function createConditionalClientProviderFromSpec(
  spec: ConditionalClientProviderSpec,
  options: ConditionalClientProviderOptions
): Provider {
  return {
    provide: spec.token,
    inject: conditionalClientProviderInject(spec, options),
    useFactory: (...args: unknown[]) => {
      const { registration, runtime, moduleRef, discovery } = conditionalClientProviderArgs(spec, options, args);
      if (options.checkEnabled && !spec.isEnabled(registration)) {
        return null;
      }
      return spec.create(registration, runtime, moduleRef, discovery);
    }
  };
}

function conditionalClientProviderInject(
  spec: ConditionalClientProviderSpec,
  options: ConditionalClientProviderOptions
): InjectionToken[] {
  return [
    ...(options.registration === undefined ? [ZLINK_FRAMEWORK_REGISTRATION] : []),
    ...(spec.requiresRuntime ? [ZLINK_FRAMEWORK_RUNTIME] : []),
    ModuleRef,
    DiscoveryService
  ];
}

function conditionalClientProviderArgs(
  spec: ConditionalClientProviderSpec,
  options: ConditionalClientProviderOptions,
  args: readonly unknown[]
): {
  readonly registration: ZLinkFrameworkRegistration;
  readonly runtime: FrameworkRuntimeHost | undefined;
  readonly moduleRef: ModuleRef;
  readonly discovery: DiscoveryService;
} {
  let index = 0;
  const registration = options.registration ?? args[index++] as ZLinkFrameworkRegistration;
  const runtime = spec.requiresRuntime ? args[index++] as FrameworkRuntimeHost : undefined;
  const moduleRef = args[index++] as ModuleRef;
  const discovery = args[index++] as DiscoveryService;
  return { registration, runtime, moduleRef, discovery };
}

function requireRuntime(runtime: FrameworkRuntimeHost | undefined): FrameworkRuntimeHost {
  if (runtime === undefined) {
    throw new framework.ZLinkConfigurationException('ZLink runtime host is not available.');
  }
  return runtime;
}

export function conditionalClientTokens(): InjectionToken[] {
  return [
    ZLINK_SPOT_MANAGER,
    ZLINK_SPOT_OUTBOUND,
    ZLINK_SPOT_PUBLISHER_CLIENT,
    ZLINK_ACTOR_CLIENT,
    ZLINK_ACTOR_MANAGER,
    ZLINK_SPOT_REF_RESOLVER,
    ZLINK_ACTOR_SPOT_REF_RESOLVER,
    ZLINK_LOCATION_RUNTIME_QUERY
  ];
}

function hasLocationStores(registration: ZLinkFrameworkRegistration): boolean {
  return registration.locations.useInMemoryStores
    || registration.locations.storeInstance !== undefined;
}

export function createRuntimeHost(
  registration: ZLinkFrameworkRegistration,
  moduleRef: ModuleRef,
  discovery: DiscoveryService
): RuntimeHostWithNestLifecycle {
  const runtime = new framework.ZLinkFrameworkRuntimeHost({
    registration,
    providerResolver: createProviderResolver(moduleRef, discovery)
  }) as RuntimeHostWithNestLifecycle;
  runtime.onModuleInit = async () => {
    registerDiscoveredRuntimeEventHandlers(runtime.eventPublisher, discovery);
    await runtime.start();
  };
  runtime.onModuleDestroy = async () => {
    await runtime.stop();
  };
  runtime.onApplicationBootstrap = async () => {};
  runtime.onApplicationShutdown = async () => {};
  return runtime;
}

function registerDiscoveredRuntimeEventHandlers(
  publisher: ZLinkRuntimeEventPublisher,
  discovery: DiscoveryService
): void {
  for (const handler of discoverRuntimeEventHandlers(discovery)) {
    if (!claimRuntimeEventHandler(publisher, handler)) {
      continue;
    }
    publisher.register(handler);
  }
}

function discoverRuntimeEventHandlers(discovery: DiscoveryService): ZLinkRuntimeEventHandler<ZLinkRuntimeEvent>[] {
  const handlers: ZLinkRuntimeEventHandler<ZLinkRuntimeEvent>[] = [];
  for (const wrapper of discovery.getProviders()) {
    const token = wrapper.metatype;
    const instance = wrapper.instance;
    if (
      instance !== undefined
      && instance !== null
      && isNestRuntimeEventHandler(token)
      && typeof (instance as { handle?: unknown }).handle === 'function'
    ) {
      handlers.push(instance as ZLinkRuntimeEventHandler<ZLinkRuntimeEvent>);
    }
  }
  return handlers;
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
      try {
        return await moduleRef.resolve(type, undefined, { strict: false });
      } catch {
        // Fall back to direct construction through Nest for classes that are
        // not registered as providers.
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

export function providerToken(provider: Provider): InjectionToken {
  return typeof provider === 'function' ? provider : provider.provide;
}

async function createSpotManager(
  registration: ZLinkFrameworkRegistration,
  runtime: FrameworkRuntimeHost,
  moduleRef: ModuleRef | undefined,
  discovery: DiscoveryService | undefined
): Promise<unknown> {
  const hostSpotOptions = runtime.createSpotManagerOptions?.() as Record<string, unknown> | undefined;
  const manager = new framework.DefaultZLinkSpotManager({
    spotFactories: [...registration.spotFactories],
    spotTimerHandlers: [...registration.spotNodes.values()]
      .flatMap((spotNode) => [...(spotNode.spotTimerHandlers ?? [])]),
    spotPacketHandlers: [...registration.spotNodes.values()]
      .flatMap((spotNode) => [...(spotNode.spotPacketHandlers ?? [])]),
    spotSubscriptionHandlers: [...registration.spotNodes.values()]
      .flatMap((spotNode) => [...(spotNode.spotSubscriptionHandlers ?? [])]),
    spotActorSendHandlers: [...registration.spotNodes.values()]
      .flatMap((spotNode) => [...(spotNode.spotActorSendHandlers ?? [])]),
    spotActorRequestHandlers: [...registration.spotNodes.values()]
      .flatMap((spotNode) => [...(spotNode.spotActorRequestHandlers ?? [])]),
    ...hostSpotOptions,
    spotRouteResolver: hostSpotOptions?.spotRouteResolver,
    routedTransport: runtime.routeTransport,
    providerResolver: moduleRef === undefined ? undefined : createProviderResolver(moduleRef, discovery),
    workerRuntime: new framework.ZLinkSpotWorkerRuntime(registration.worker)
  });
  runtime.setSpotManager?.(manager);
  return manager;
}

async function createSpotOutbound(
  _registration: ZLinkFrameworkRegistration,
  runtime: FrameworkRuntimeHost,
  _moduleRef: ModuleRef | undefined,
  _discovery: DiscoveryService | undefined
): Promise<unknown> {
  const hostSpotOptions = runtime.createSpotManagerOptions?.() as Record<string, unknown> | undefined;
  return new framework.DefaultZLinkSpotOutbound(
    new framework.ZLinkSpotSerialExecutor(),
    undefined,
    undefined,
    undefined,
    runtime.routeTransport,
    hostSpotOptions?.spotRouterChannelIdForMesh as ((meshName: string) => string) | undefined
  );
}
