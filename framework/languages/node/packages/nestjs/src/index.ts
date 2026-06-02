import { createRequire } from 'node:module';
import path from 'node:path';
import type {
  ZLinkFrameworkRegistration,
  ZLinkFrameworkRegistrationOptions,
  ZLinkRegistryOptions,
  ZLinkRegistryQueryClientOptions
} from '@zlink-systems/framework';

type FrameworkModule = typeof import('@zlink-systems/framework');

const framework = loadFramework();

export type InjectionToken = string | symbol | Function;

export interface Provider<T = unknown> {
  readonly provide: InjectionToken;
  readonly useValue?: T;
  readonly useFactory?: (...args: never[]) => T | Promise<T>;
  readonly inject?: readonly InjectionToken[];
}

export interface DynamicModule {
  readonly module: Function;
  readonly providers: readonly Provider[];
  readonly exports: readonly InjectionToken[];
}

export interface ZLinkModuleAsyncOptions {
  readonly useFactory: (...args: unknown[]) => ZLinkModuleOptions | Promise<ZLinkModuleOptions>;
  readonly inject?: readonly InjectionToken[];
}

export type ZLinkModuleOptions = ZLinkFrameworkRegistrationOptions;

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

export class ZLinkModule {
  static forRoot(options: ZLinkModuleOptions = {}): DynamicModule {
    return createZLinkDynamicModule(framework.createFrameworkRegistration(options));
  }

  static forRootAsync(options: ZLinkModuleAsyncOptions): DynamicModule {
    const registrationProvider: Provider<Promise<ZLinkFrameworkRegistration>> = {
      provide: ZLINK_FRAMEWORK_REGISTRATION,
      inject: options.inject,
      useFactory: async (...args: unknown[]) => framework.createFrameworkRegistration(await options.useFactory(...args))
    };

    return {
      module: ZLinkModule,
      providers: [
        registrationProvider,
        {
          provide: ZLINK_FRAMEWORK_RUNTIME,
          inject: [ZLINK_FRAMEWORK_REGISTRATION],
          useFactory: (registration: ZLinkFrameworkRegistration) => new framework.ZLinkFrameworkRuntimeHost({ registration })
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
      exports: providers.map((provider) => provider.provide)
    };
  }
}

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
    { provide: ZLINK_FRAMEWORK_RUNTIME, useValue: new framework.ZLinkFrameworkRuntimeHost({ registration }) },
    ...alwaysAvailableClientProviders(registration),
    ...conditionalClientProviders(registration)
  ];

  return {
    module: ZLinkModule,
    providers,
    exports: providers.map((provider) => provider.provide)
  };
}

function alwaysAvailableClientProviders(registration?: ZLinkFrameworkRegistration): Provider[] {
  if (registration === undefined) {
    return [
      {
        provide: ZLINK_CHANNEL_CLIENT,
        inject: [ZLINK_FRAMEWORK_REGISTRATION],
        useFactory: (resolved: ZLinkFrameworkRegistration) => new framework.DefaultZLinkChannelClient(resolved)
      },
      {
        provide: ZLINK_FANOUT_CLIENT,
        inject: [ZLINK_FRAMEWORK_REGISTRATION],
        useFactory: (resolved: ZLinkFrameworkRegistration) => new framework.DefaultZLinkFanoutClient(resolved)
      },
      {
        provide: ZLINK_ROUTE_CLIENT,
        inject: [ZLINK_FRAMEWORK_REGISTRATION],
        useFactory: (resolved: ZLinkFrameworkRegistration) => new framework.DefaultZLinkRouteClient(resolved)
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
    { provide: ZLINK_CHANNEL_CLIENT, useValue: new framework.DefaultZLinkChannelClient(registration) },
    { provide: ZLINK_FANOUT_CLIENT, useValue: new framework.DefaultZLinkFanoutClient(registration) },
    { provide: ZLINK_ROUTE_CLIENT, useValue: new framework.DefaultZLinkRouteClient(registration) },
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
      { provide: ZLINK_SPOT_OUTBOUND, useValue: createSpotOutbound(registration) }
    );
  }

  if (framework.hasSpotPublisherClient(registration)) {
    providers.push({
      provide: ZLINK_SPOT_PUBLISHER_CLIENT,
      useValue: new framework.DefaultZLinkSpotPublisherClient(registration)
    });
  }

  if (framework.hasActorManager(registration)) {
    providers.push({
      provide: ZLINK_ACTOR_MANAGER,
      useValue: new framework.DefaultZLinkActorManager({ actorFactories: registration.actorFactories })
    });
  }

  if (framework.hasSpotRemoteAddressResolver(registration)) {
    providers.push({
      provide: ZLINK_SPOT_REMOTE_ADDRESS_RESOLVER,
      useValue: new framework.DefaultZLinkUnavailableSpotRemoteAddressResolver()
    });
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
      inject: [ZLINK_FRAMEWORK_REGISTRATION],
      useFactory: (registration: ZLinkFrameworkRegistration) => {
        ensureCapability(framework.hasSpotNode(registration), ZLINK_SPOT_OUTBOUND);
        return createSpotOutbound(registration);
      }
    },
    {
      provide: ZLINK_SPOT_PUBLISHER_CLIENT,
      inject: [ZLINK_FRAMEWORK_REGISTRATION],
      useFactory: (registration: ZLinkFrameworkRegistration) => {
        ensureCapability(framework.hasSpotPublisherClient(registration), ZLINK_SPOT_PUBLISHER_CLIENT);
        return new framework.DefaultZLinkSpotPublisherClient(registration);
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
        return new framework.DefaultZLinkUnavailableSpotRemoteAddressResolver();
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

function ensureCapability(enabled: boolean, token: InjectionToken): void {
  if (!enabled) {
    throw new framework.ZLinkConfigurationException(`Provider ${String(token)} requires a matching ZLink capability.`);
  }
}

function createSpotManager(registration: ZLinkFrameworkRegistration): InstanceType<FrameworkModule['DefaultZLinkSpotManager']> {
  return new framework.DefaultZLinkSpotManager({ spotFactories: [...registration.spotFactories] });
}

function createSpotOutbound(_registration: ZLinkFrameworkRegistration): InstanceType<FrameworkModule['DefaultZLinkSpotOutbound']> {
  return new framework.DefaultZLinkSpotOutbound(new framework.ZLinkSpotSerialExecutor());
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
