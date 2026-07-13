import type { DiscoveryService, ModuleRef } from '@nestjs/core';
import type {
  Type,
  ZLinkChannelOptions,
  ZLinkFrameworkRegistrationOptions,
  ZLinkRouteChannelOptions,
} from '@zlink-systems/framework/nest-integration';
import {
  ZLINK_MODULE_OPTIONS_BRAND,
  type ZLinkModuleOptions,
  type ZLinkNestModuleRegistrationOptions,
  type ZLinkNestTypeResolver
} from './contracts';
import {
  discoverProviderRefs,
  discoverSpotActorProviderRefs,
  discoverSpotProviderRefs,
  discoverSpotTimerProviderRefs,
  type DiscoveredNestSpotActorProvider,
  type DiscoveredNestSpotProvider,
  type DiscoveredNestSpotTimerProvider
} from './provider-discovery';
import {
  createDiscoveredPublishHandlers,
  createDiscoveredRequestHandlers,
  createDiscoveredSendHandlers,
  createManualPublishHandlers,
  createManualRequestHandlers,
  createManualRouteRequestHandlers,
  createManualRouteSendHandlers,
  createManualSendHandlers
} from './handler-adapters';
import { framework } from './framework-loader';
import { copyRouteInternalState } from './options-builder';
import { SpotNodeHandlerRegistry } from './spot-node-handler-registry';


export function hasNestHandlerDiscovery(options: ZLinkNestModuleRegistrationOptions): boolean {
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


export function createDiscoveredOptions(
  options: ZLinkNestModuleRegistrationOptions,
  discovery: DiscoveryService,
  moduleRef: ModuleRef
): ZLinkFrameworkRegistrationOptions {
  const registrationOptions = createRegistrationOptions(options);
  const channels: Record<string, ZLinkChannelOptions> = { ...(registrationOptions.channels ?? {}) };
  const routerMeshes = new Map<string, ZLinkRouteChannelOptions>();
  const providerRefs = discoverProviderRefs(discovery, moduleRef);
  const spotActorProviderRefs = discoverSpotActorProviderRefs(discovery, moduleRef);
  const spotProviderRefs = discoverSpotProviderRefs(discovery, moduleRef);
  const spotTimerProviderRefs = discoverSpotTimerProviderRefs(discovery, moduleRef);
  const spotNodes = createDiscoveredSpotNodeOptions(
    registrationOptions.spotNodes,
    spotActorProviderRefs,
    spotProviderRefs,
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
    if (typeof routeChannel !== 'string') {
      copyRouteInternalState(routeChannel, normalized);
    }
    routerMeshes.set(normalized.routerChannelId, normalized);
  }
  for (const [routerMeshName, routerMesh] of Object.entries(options.routerMeshes ?? {})) {
    const existing = routerMeshes.get(routerMeshName) ?? { routerChannelId: routerMeshName };
    const {
      handlerGroups: _handlerGroups,
      requestHandlerTypes: _requestHandlerTypes,
      sendHandlerTypes: _sendHandlerTypes,
      ...routeTransportOptions
    } = routerMesh;
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
    const normalized = {
      ...existing,
      ...routeTransportOptions,
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
    };
    copyRouteInternalState(existing, normalized);
    copyRouteInternalState(routerMesh, normalized);
    routerMeshes.set(routerMeshName, normalized);
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
  spotRefs: readonly DiscoveredNestSpotProvider[] = [],
  timerRefs: readonly DiscoveredNestSpotTimerProvider[] = []
): ZLinkFrameworkRegistrationOptions['spotNodes'] {
  if (refs.length === 0 && spotRefs.length === 0 && timerRefs.length === 0) {
    return value;
  }
  const registry = SpotNodeHandlerRegistry.from(value);
  if (registry.isEmpty) {
    throw new framework.ZLinkConfigurationException('ZLink SPOT actor handlers require a registered SpotNode.');
  }

  addDiscoveredSpotTimers(registry, timerRefs);
  addDiscoveredSpotHandlers(registry, spotRefs);
  addDiscoveredSpotActorHandlers(registry, refs);
  return registry.toOptions();
}

function addDiscoveredSpotTimers(
  registry: SpotNodeHandlerRegistry,
  timerRefs: readonly DiscoveredNestSpotTimerProvider[]
): void {
  for (const ref of timerRefs) {
    if (ref.metadata.entrySpot !== undefined) {
      const entrySpotType = resolveNestType(ref.metadata.entrySpot, 'entrySpot');
      const targets = registry.entrySpotTargets(
        entrySpotType,
        `ZLink Entry Spot timer handler '${ref.handlerName}' targets an Entry Spot that is not registered on any SpotNode.`
      );
      for (const spotNode of targets) {
        registry.addEntrySpotTimer(spotNode, {
          entrySpotType,
          handlerType: ref.handlerKey,
          name: ref.metadata.name,
          options: ref.metadata.options,
          periodMs: ref.metadata.periodMs
        });
      }
      continue;
    }
    const spotType = resolveNestType(ref.metadata.spot, 'spot');
    const targets = registry.spotTargets(
      spotType,
      `ZLink SPOT timer handler '${ref.handlerName}' targets a Spot type that is not registered on any SpotNode.`
    );
    for (const spotNode of targets) {
      registry.addSpotTimer(spotNode, {
        handlerType: ref.handlerKey,
        name: ref.metadata.name,
        options: ref.metadata.options,
        periodMs: ref.metadata.periodMs,
        spotType
      });
    }
  }
}

function addDiscoveredSpotHandlers(
  registry: SpotNodeHandlerRegistry,
  spotRefs: readonly DiscoveredNestSpotProvider[]
): void {
  for (const ref of spotRefs) {
    if (ref.metadata.kind === 'entrySpotPacket' || ref.metadata.kind === 'entrySpotSubscription') {
      const entrySpotType = resolveNestType(ref.metadata.entrySpot, 'entrySpot');
      const targets = registry.entrySpotTargets(
        entrySpotType,
        `ZLink Entry Spot handler '${ref.handlerName}' targets an Entry Spot that is not registered on any SpotNode.`
      );
      for (const spotNode of targets) {
        if (ref.metadata.kind === 'entrySpotPacket') {
          registry.addEntrySpotPacket(spotNode, {
            entrySpotType,
            handlerType: ref.handlerKey,
            packetName: ref.metadata.packetName
          });
        } else {
          registry.addEntrySpotSubscription(spotNode, {
            entrySpotType,
            handlerType: ref.handlerKey,
            topic: requireSpotSubscriptionTopic(ref)
          });
        }
      }
      continue;
    }

    const spotType = resolveNestType(ref.metadata.spot, 'spot');
    const targets = registry.spotTargets(
      spotType,
      `ZLink SPOT handler '${ref.handlerName}' targets a Spot type that is not registered on any SpotNode.`
    );
    for (const spotNode of targets) {
      if (ref.metadata.kind === 'spotPacket') {
        registry.addSpotPacket(spotNode, {
          handlerType: ref.handlerKey,
          packetName: ref.metadata.packetName,
          spotType
        });
      } else {
        registry.addSpotSubscription(spotNode, {
          handlerType: ref.handlerKey,
          spotType,
          topic: requireSpotSubscriptionTopic(ref)
        });
      }
    }
  }
}

function addDiscoveredSpotActorHandlers(
  registry: SpotNodeHandlerRegistry,
  refs: readonly DiscoveredNestSpotActorProvider[]
): void {
  for (const ref of refs) {
    if (ref.metadata.kind === 'entrySpotActorSend' || ref.metadata.kind === 'entrySpotActorRequest') {
      const entrySpotType = resolveNestType(ref.metadata.entrySpot, 'entrySpot');
      const actorType = resolveNestType(ref.metadata.actor, 'actor');
      const targets = registry.entrySpotTargets(
        entrySpotType,
        `ZLink Entry Spot actor handler '${ref.handlerName}' targets an Entry Spot that is not registered on any SpotNode.`
      );
      for (const spotNode of targets) {
        const next = {
          actorType,
          entrySpotType,
          handlerType: ref.handlerKey,
          packetName: ref.metadata.packetName
        };
        registry.addEntrySpotActor(
          spotNode,
          ref.metadata.kind === 'entrySpotActorSend' ? 'send' : 'request',
          next
        );
      }
      continue;
    }

    const spotType = resolveNestType(ref.metadata.spot, 'spot');
    const actorType = resolveNestType(ref.metadata.actor, 'actor');
    const targets = registry.spotTargets(
      spotType,
      `ZLink SPOT actor handler '${ref.handlerName}' targets a Spot type that is not registered on any SpotNode.`
    );
    for (const spotNode of targets) {
      const next = {
        actorType,
        handlerType: ref.handlerKey,
        packetName: ref.metadata.packetName,
        spotType
      };
      registry.addSpotActor(
        spotNode,
        ref.metadata.kind === 'spotActorSend' ? 'send' : 'request',
        next
      );
    }
  }
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


function requireSpotSubscriptionTopic(ref: DiscoveredNestSpotProvider): string {
  const topic = ref.metadata.topic;
  if (topic === undefined || topic.trim().length === 0) {
    throw new framework.ZLinkConfigurationException(`ZLink SPOT subscription handler '${ref.handlerName}' requires a topic.`);
  }
  return topic;
}

export function createRegistrationOptions(options: ZLinkNestModuleRegistrationOptions): ZLinkFrameworkRegistrationOptions {
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

  for (const [name, routerMesh] of Object.entries(options.routerMeshes ?? {})) {
    const { handlerGroups: _handlerGroups, ...routeChannel } = routerMesh;
    const normalized = {
      routerChannelId: name,
      ...routeChannel
    };
    copyRouteInternalState(routerMesh, normalized);
    routeChannels.push(normalized);
  }

  return {
    actorTransferAdapters: options.actorTransferAdapters,
    actorTransferForwardWindowMs: options.actorTransferForwardWindowMs,
    channels,
    codecs: options.codecs,
    dispatch: options.dispatch,
    filters: options.filters,
    locations: options.locations,
    metrics: options.metrics,
    monitoring: options.monitoring,
    requestTimeoutMs: options.requestTimeoutMs,
    routeChannels,
    spotFactories: options.spotFactories,
    spotNodes: options.spotNodes,
    spotPublisherClients: options.spotPublisherClients,
    streamCompression: options.streamCompression,
    streamNodes: options.streams,
    worker: options.worker
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

export function assertBuiltModuleOptions(options: ZLinkModuleOptions): ZLinkNestModuleRegistrationOptions {
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
