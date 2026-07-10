import { createRequire } from 'node:module';
import path from 'node:path';
import type {
  IZLinkLocationRuntimeQuery,
  ZLinkChannelRuntimeOptions,
  ZLinkFrameworkRegistration,
  ZLinkFrameworkRegistrationOptions,
  ZLinkProviderResolver,
  ZLinkRuntimeEventPublisher
} from '@zlink-systems/framework';

export interface FrameworkRuntimeHost {
  readonly channelTransport: unknown;
  readonly routeTransport: unknown;
  readonly channelRuntimeOptions: ZLinkChannelRuntimeOptions;
  readonly spotPublisherTransport: unknown;
  readonly streamBindingRuntime: unknown;
  readonly boundSessionFactory: {
    create(actorId: string): unknown;
  };
  readonly isStarted: boolean;
  readonly locationRuntimeQuery?: IZLinkLocationRuntimeQuery;
  readonly eventPublisher: ZLinkRuntimeEventPublisher;
  start(): Promise<void>;
  stop(): Promise<void>;
  onApplicationBootstrap(): Promise<void>;
  onApplicationShutdown(): Promise<void>;
  setActorManager?(actorManager: unknown): void;
  setSpotManager?(spotManager: unknown): void;
  createActorManagerOptions?(): object;
  createActorClientOptions?(): Record<string, unknown>;
  createLocationRefResolver?(): unknown;
  createSpotManagerOptions?(): object;
}

export interface FrameworkModule {
  readonly ZLinkConfigurationException: new (message: string) => Error;
  readonly ZLinkFrameworkRuntimeHost: new (options: {
    readonly registration: ZLinkFrameworkRegistration;
    readonly providerResolver?: ZLinkProviderResolver;
  }) => FrameworkRuntimeHost;
  readonly DefaultZLinkChannelClient: new (registration: ZLinkFrameworkRegistration, transport: unknown) => unknown;
  readonly DefaultZLinkFanoutClient: new (registration: ZLinkFrameworkRegistration, transport: unknown) => unknown;
  readonly DefaultZLinkRouteClient: new (registration: ZLinkFrameworkRegistration, transport: unknown) => unknown;
  readonly DefaultZLinkActorClient: new (options: Record<string, unknown>) => unknown;
  readonly DefaultZLinkSpotPublisherClient: new (registration: ZLinkFrameworkRegistration, transport: unknown) => unknown;
  readonly DefaultZLinkActorManager: new (options: Record<string, unknown>) => unknown;
  readonly DefaultZLinkSpotManager: new (options: Record<string, unknown>) => unknown;
  readonly DefaultZLinkSpotOutbound: new (...args: unknown[]) => unknown;
  readonly ZLinkSpotSerialExecutor: new () => unknown;
  readonly ZLinkSpotWorkerRuntime: new (options?: unknown) => unknown;
  createFrameworkRegistration(options: ZLinkFrameworkRegistrationOptions): ZLinkFrameworkRegistration;
  hasSpotNode(registration: ZLinkFrameworkRegistration): boolean;
  hasActorManager(registration: ZLinkFrameworkRegistration): boolean;
  hasSpotPublisherClient(registration: ZLinkFrameworkRegistration): boolean;
}

export function loadFramework(): FrameworkModule {
  const requireFramework = createRequire(__filename);
  return requireFramework(path.resolve(__dirname, '../../framework/dist/nest-integration')) as FrameworkModule;
}
