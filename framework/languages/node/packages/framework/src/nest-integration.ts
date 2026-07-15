export {
  ZLinkConfigurationException,
  createFrameworkRegistration,
  hasActorManager,
  hasSpotNode,
  hasSpotPublisherClient
} from './contracts/Configuration/Registration';
export type * from './contracts/Configuration/RegistrationTypes';
export type * from './contracts';
export {
  registerActorFactory,
  registerActorTransferAdapter,
  registerEntrySpot,
  registerSpotFactory,
  validateActorTransferForwardWindow
} from './contracts/Configuration/RegistrationBuilderPolicy';

import type {
  ZLinkLocationRuntimeQuery,
  ZLinkActorClient,
  ZLinkActorManager,
  ZLinkChannelClient,
  ZLinkCodecRegistryBuilder,
  ZLinkCodecRegistrar,
  ZLinkDispatchOptions,
  ZLinkDispatchOptionsBuilder,
  ZLinkFanoutClient,
  ZLinkProviderResolver,
  ZLinkRouteClient,
  ZLinkRuntimeEventPublisher,
  ZLinkSpotManager,
  ZLinkSpotOutbound,
  ZLinkSpotPublisherClient,
  ZLinkSpotHandleResolver,
  ZLinkStreamCompressionBuilder,
  ZLinkStreamCompressionCodec
} from './contracts';
import type { ZLinkBoundSessionFactory } from './runtime/streams/session-context';
import type { ZLinkFrameworkRegistration } from './contracts/Configuration/Registration';
import type {
  ZLinkCodecSerializerRegistration,
  ZLinkStreamCodecRegistration
} from './contracts/Configuration/RegistrationTypes';
import {
  DefaultDispatchOptionsBuilder,
  DefaultStreamCompressionBuilder
} from './contracts/Configuration/RegistrationBuilders';
import { RegistrationCodecRegistryBuilder } from './contracts/Configuration/RegistrationCodecRegistry';
import {
  DefaultZLinkChannelClient,
  DefaultZLinkFanoutClient,
  DefaultZLinkRouteClient,
  DefaultZLinkSpotPublisherClient
} from './runtime/channels';
import { DefaultZLinkActorClient, DefaultZLinkActorManager } from './runtime/actors';
import { ZLinkFrameworkRuntimeHost } from './runtime/host';
import {
  DefaultZLinkSpotManager,
  DefaultZLinkSpotOutbound,
  ZLinkSpotSerialExecutor
} from './runtime/spots';
import { ZLinkSpotWorkerRuntime } from './runtime/workers';
import { captureZLinkExecutionTurn } from './runtime/execution';
export {
  registerHandlerFilterScope as registerIntegrationHandlerFilterScope,
  type ZLinkHandlerFilterScopeRunner
} from './runtime/channels/handler-filter-scope';

export interface ZLinkNestIntegrationRuntimeHost {
  readonly channelRuntimeOptions: unknown;
  readonly boundSessionFactory: ZLinkBoundSessionFactory;
  readonly eventPublisher: ZLinkRuntimeEventPublisher;
  readonly locationRuntimeQuery?: ZLinkLocationRuntimeQuery;
  createLocationHandleResolver(): ZLinkSpotHandleResolver | undefined;
  start(): Promise<void>;
  stop(): Promise<void>;
  drain(deadlineMs?: number, signal?: AbortSignal): Promise<import('./contracts').ZLinkDrainResult>;
  awaitDrained(signal?: AbortSignal): Promise<import('./contracts').ZLinkDrainResult>;
  isReady(): boolean;
  onApplicationBootstrap?(): Promise<void> | void;
  onApplicationShutdown?(): Promise<void> | void;
}

export function createIntegrationDispatchOptionsBuilder(
  dispatch: ZLinkDispatchOptions
): ZLinkDispatchOptionsBuilder {
  return new DefaultDispatchOptionsBuilder(dispatch);
}

export function createIntegrationStreamCompressionBuilder(
  options: { disabled?: boolean; codec?: ZLinkStreamCompressionCodec }
): ZLinkStreamCompressionBuilder {
  return new DefaultStreamCompressionBuilder(options);
}

export function createIntegrationCodecRegistryBuilder(
  options: {
    serializers: ZLinkCodecSerializerRegistration[];
    streamCodecs: ZLinkStreamCodecRegistration[];
  }
): ZLinkCodecRegistryBuilder & ZLinkCodecRegistrar {
  return new RegistrationCodecRegistryBuilder(options);
}

export function createIntegrationRuntimeHost(
  registration: ZLinkFrameworkRegistration,
  providerResolver?: ZLinkProviderResolver
): ZLinkNestIntegrationRuntimeHost {
  return new ZLinkFrameworkRuntimeHost({ registration, providerResolver });
}

export function createIntegrationChannelClient(
  registration: ZLinkFrameworkRegistration,
  runtime: ZLinkNestIntegrationRuntimeHost
): ZLinkChannelClient {
  return new DefaultZLinkChannelClient(registration, runtimeHost(runtime).channelTransport);
}

export function createIntegrationFanoutClient(
  registration: ZLinkFrameworkRegistration,
  runtime: ZLinkNestIntegrationRuntimeHost
): ZLinkFanoutClient {
  return new DefaultZLinkFanoutClient(registration, runtimeHost(runtime).channelTransport);
}

export function createIntegrationRouteClient(
  registration: ZLinkFrameworkRegistration,
  runtime: ZLinkNestIntegrationRuntimeHost
): ZLinkRouteClient {
  const host = runtimeHost(runtime);
  return new DefaultZLinkRouteClient(
    registration,
    host.routeTransport,
    host.spotRouterChannelIdForMesh
  );
}

export function createIntegrationSpotPublisherClient(
  registration: ZLinkFrameworkRegistration,
  runtime: ZLinkNestIntegrationRuntimeHost
): ZLinkSpotPublisherClient {
  return new DefaultZLinkSpotPublisherClient(registration, runtimeHost(runtime).spotPublisherTransport);
}

export function createIntegrationActorClient(runtime: ZLinkNestIntegrationRuntimeHost): ZLinkActorClient {
  const host = runtimeHost(runtime);
  return new DefaultZLinkActorClient(host.createActorClientOptions());
}

export function createIntegrationActorManager(
  registration: ZLinkFrameworkRegistration,
  runtime: ZLinkNestIntegrationRuntimeHost,
  providerResolver?: ZLinkProviderResolver
): ZLinkActorManager {
  const host = runtimeHost(runtime);
  const runtimeOptions = host.createActorManagerOptions();
  const manager = new DefaultZLinkActorManager({
    actorFactories: registration.actorFactories,
    ...runtimeOptions,
    boundSessionFactory: runtimeOptions.boundSessionFactory ?? host.boundSessionFactory.create.bind(host.boundSessionFactory),
    providerResolver
  });
  host.setActorManager(manager);
  return manager;
}

export function createIntegrationSpotManager(
  registration: ZLinkFrameworkRegistration,
  runtime: ZLinkNestIntegrationRuntimeHost,
  providerResolver?: ZLinkProviderResolver
): ZLinkSpotManager {
  const host = runtimeHost(runtime);
  const runtimeOptions = host.createSpotManagerOptions();
  const manager = new DefaultZLinkSpotManager({
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
    ...runtimeOptions,
    spotRouteResolver: runtimeOptions.spotRouteResolver,
    routedTransport: host.routeTransport,
    providerResolver,
    workerRuntime: new ZLinkSpotWorkerRuntime(registration.worker)
  });
  host.setSpotManager(manager);
  return manager;
}

export function createIntegrationSpotOutbound(runtime: ZLinkNestIntegrationRuntimeHost): ZLinkSpotOutbound {
  const host = runtimeHost(runtime);
  const runtimeOptions = host.createSpotManagerOptions();
  return new DefaultZLinkSpotOutbound(
    new ZLinkSpotSerialExecutor(),
    undefined,
    undefined,
    undefined,
    host.routeTransport,
    runtimeOptions.spotRouterChannelIdForMesh
  );
}

export function createIntegrationHttpExecutionScheduler(runtime: ZLinkNestIntegrationRuntimeHost) {
  return {
    capture: captureZLinkExecutionTurn,
    reportError(error: unknown): void {
      const host = runtimeHost(runtime);
      host.errorSink?.reportRuntimeTaskException('http client submit', error);
    }
  };
}

function runtimeHost(runtime: ZLinkNestIntegrationRuntimeHost): ZLinkFrameworkRuntimeHost {
  return runtime as ZLinkFrameworkRuntimeHost;
}
