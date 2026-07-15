import 'reflect-metadata';
import type { ZLinkNestFrameworkOptionsBuilder } from './contracts';
import { createZLinkNestFrameworkOptionsBuilder } from './options-builder';

export type {
  ZLinkModuleFactoryOptions,
  ZLinkModuleOptions,
  ZLinkNestClientServerChannelBuilder,
  ZLinkNestCodecRegistryBuilder,
  ZLinkNestEntrySpotActorRequestHandlerOptions,
  ZLinkNestEntrySpotActorSendHandlerOptions,
  ZLinkNestEntrySpotPacketHandlerOptions,
  ZLinkNestEntrySpotSubscriptionHandlerOptions,
  ZLinkNestFanoutChannelBuilder,
  ZLinkNestFrameworkAdditionalOptions,
  ZLinkNestFrameworkOptionsBuilder,
  ZLinkNestHandlerOptions,
  ZLinkNestModuleMetadata,
  ZLinkNestModuleRoleRoot,
  ZLinkNestProviderDiscoveryOptions,
  ZLinkNestProviderDiscoveryRoot,
  ZLinkNestRouterMeshBuilder,
  ZLinkNestSpotActorRequestHandlerOptions,
  ZLinkNestSpotActorSendHandlerOptions,
  ZLinkNestSpotNodeBuilder,
  ZLinkNestSpotPacketHandlerOptions,
  ZLinkNestSpotSubscriptionHandlerOptions,
  ZLinkNestSpotTimerHandlerOptions,
  ZLinkNestStreamNodeBuilder,
  ZLinkNestTypeResolver
} from './contracts';
export * from './decorators';
export * from './module';
export * from './tokens';
export * from './drain-health-indicator';
export * from './http-client-module';
export type { ZLinkNestHandlerKind } from './handler-metadata';

export function zlinkFramework(): ZLinkNestFrameworkOptionsBuilder {
  return createZLinkNestFrameworkOptionsBuilder();
}
