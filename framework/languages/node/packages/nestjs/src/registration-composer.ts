import type { DiscoveryService, ModuleRef } from '@nestjs/core';
import type {
  Type,
  ZLinkChannelOptions,
  ZLinkEntrySpotActorRequestHandlerRegistration,
  ZLinkEntrySpotActorSendHandlerRegistration,
  ZLinkEntrySpotPacketHandlerRegistration,
  ZLinkEntrySpotSubscriptionHandlerRegistration,
  ZLinkFrameworkRegistrationOptions,
  ZLinkRouteChannelOptions,
  ZLinkSpotActorRequestHandlerRegistration,
  ZLinkSpotActorSendHandlerRegistration,
  ZLinkSpotNodeOptions,
  ZLinkSpotPacketHandlerRegistration,
  ZLinkSpotSubscriptionHandlerRegistration
} from '@zlink-systems/framework';
import {
  ZLINK_MODULE_OPTIONS_BRAND,
  type Mutable,
  type ZLinkModuleOptions,
  type ZLinkNestModuleRegistrationOptions,
  type ZLinkNestTypeResolver
} from './contracts';
import {
  createDiscoveredPublishHandlers,
  createDiscoveredRequestHandlers,
  createDiscoveredSendHandlers,
  createManualPublishHandlers,
  createManualRequestHandlers,
  createManualRouteRequestHandlers,
  createManualRouteSendHandlers,
  createManualSendHandlers,
  discoverProviderRefs,
  discoverSpotActorProviderRefs,
  discoverSpotProviderRefs,
  discoverSpotTimerProviderRefs,
  type DiscoveredNestSpotActorProvider,
  type DiscoveredNestSpotProvider,
  type DiscoveredNestSpotTimerProvider
} from './discovery';
import { loadFramework } from './framework-loader';
import { copyRouteInternalState } from './options-builder';

const framework = loadFramework();

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
  const spotNodes = toMutableSpotNodeRecord(value);
  const spotNodeEntries = Object.entries(spotNodes);
  if (spotNodeEntries.length === 0) {
    throw new framework.ZLinkConfigurationException('ZLink SPOT actor handlers require a registered SpotNode.');
  }

  addDiscoveredSpotTimers(spotNodeEntries, timerRefs);
  addDiscoveredSpotHandlers(spotNodeEntries, spotRefs);
  addDiscoveredSpotActorHandlers(spotNodeEntries, refs);
  return spotNodes;
}

type MutableSpotNodeEntry = [string, Mutable<ZLinkSpotNodeOptions>];

function addDiscoveredSpotTimers(
  spotNodeEntries: readonly MutableSpotNodeEntry[],
  timerRefs: readonly DiscoveredNestSpotTimerProvider[]
): void {
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
}

function addDiscoveredSpotHandlers(
  spotNodeEntries: readonly MutableSpotNodeEntry[],
  spotRefs: readonly DiscoveredNestSpotProvider[]
): void {
  for (const ref of spotRefs) {
    if (ref.metadata.kind === 'entrySpotPacket' || ref.metadata.kind === 'entrySpotSubscription') {
      const entrySpotType = resolveNestType(ref.metadata.entrySpot, 'entrySpot');
      const matches = spotNodeEntries.filter(([, spotNode]) => spotNode.entrySpotType === entrySpotType);
      if (matches.length === 0) {
        throw new framework.ZLinkConfigurationException(
          `ZLink Entry Spot handler '${ref.handlerName}' targets an Entry Spot that is not registered on any SpotNode.`
        );
      }
      for (const [, spotNode] of matches) {
        if (ref.metadata.kind === 'entrySpotPacket') {
          const next = {
            entrySpotType,
            handlerType: ref.handlerKey,
            packetName: ref.metadata.packetName
          };
          assertUniqueEntrySpotPacketHandler(spotNode.entrySpotPacketHandlers, next);
          spotNode.entrySpotPacketHandlers = [
            ...(spotNode.entrySpotPacketHandlers ?? []),
            next
          ];
        } else {
          const next = {
            entrySpotType,
            handlerType: ref.handlerKey,
            topic: requireSpotSubscriptionTopic(ref)
          };
          assertUniqueEntrySpotSubscriptionHandler(spotNode.entrySpotSubscriptionHandlers, next);
          spotNode.entrySpotSubscriptionHandlers = [
            ...(spotNode.entrySpotSubscriptionHandlers ?? []),
            next
          ];
        }
      }
      continue;
    }

    const spotType = resolveNestType(ref.metadata.spot, 'spot');
    const matches = spotNodeEntries.filter(([, spotNode]) => (spotNode.spotFactories ?? []).includes(spotType));
    if (matches.length === 0) {
      throw new framework.ZLinkConfigurationException(
        `ZLink SPOT handler '${ref.handlerName}' targets a Spot type that is not registered on any SpotNode.`
      );
    }
    for (const [, spotNode] of matches) {
      if (ref.metadata.kind === 'spotPacket') {
        const next = {
          handlerType: ref.handlerKey,
          packetName: ref.metadata.packetName,
          spotType
        };
        assertUniqueSpotPacketHandler(spotNode.spotPacketHandlers, next);
        spotNode.spotPacketHandlers = [
          ...(spotNode.spotPacketHandlers ?? []),
          next
        ];
      } else {
        const next = {
          handlerType: ref.handlerKey,
          spotType,
          topic: requireSpotSubscriptionTopic(ref)
        };
        assertUniqueSpotSubscriptionHandler(spotNode.spotSubscriptionHandlers, next);
        spotNode.spotSubscriptionHandlers = [
          ...(spotNode.spotSubscriptionHandlers ?? []),
          next
        ];
      }
    }
  }
}

function addDiscoveredSpotActorHandlers(
  spotNodeEntries: readonly MutableSpotNodeEntry[],
  refs: readonly DiscoveredNestSpotActorProvider[]
): void {
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
  assertUniqueRegistration(
    existing,
    next,
    (handler) =>
      handler.entrySpotType === next.entrySpotType &&
      handler.actorType === next.actorType &&
      handler.packetName === next.packetName,
    `Duplicate Entry Spot actor handler '${next.entrySpotType.name}:${next.actorType.name}:${next.packetName}'.`
  );
}

function assertUniqueSpotActorHandler(
  existing: readonly (ZLinkSpotActorSendHandlerRegistration | ZLinkSpotActorRequestHandlerRegistration)[] | undefined,
  next: ZLinkSpotActorSendHandlerRegistration | ZLinkSpotActorRequestHandlerRegistration
): void {
  assertUniqueRegistration(
    existing,
    next,
    (handler) =>
      handler.spotType === next.spotType &&
      handler.actorType === next.actorType &&
      handler.packetName === next.packetName,
    `Duplicate SPOT actor handler '${next.spotType.name}:${next.actorType.name}:${next.packetName}'.`
  );
}

function assertUniqueEntrySpotPacketHandler(
  existing: readonly ZLinkEntrySpotPacketHandlerRegistration[] | undefined,
  next: ZLinkEntrySpotPacketHandlerRegistration
): void {
  assertUniqueRegistration(
    existing,
    next,
    (handler) =>
      handler.entrySpotType === next.entrySpotType &&
      (handler.packetName ?? handler.handlerType.name) === (next.packetName ?? next.handlerType.name),
    `Duplicate Entry Spot packet handler '${next.entrySpotType.name}:${next.packetName ?? next.handlerType.name}'.`
  );
}

function assertUniqueEntrySpotSubscriptionHandler(
  existing: readonly ZLinkEntrySpotSubscriptionHandlerRegistration[] | undefined,
  next: ZLinkEntrySpotSubscriptionHandlerRegistration
): void {
  assertUniqueRegistration(
    existing,
    next,
    (handler) =>
      handler.entrySpotType === next.entrySpotType &&
      handler.topic === next.topic,
    `Duplicate Entry Spot subscription handler '${next.entrySpotType.name}:${next.topic}'.`
  );
}

function assertUniqueSpotPacketHandler(
  existing: readonly ZLinkSpotPacketHandlerRegistration[] | undefined,
  next: ZLinkSpotPacketHandlerRegistration
): void {
  assertUniqueRegistration(
    existing,
    next,
    (handler) =>
      handler.spotType === next.spotType &&
      (handler.packetName ?? handler.handlerType.name) === (next.packetName ?? next.handlerType.name),
    `Duplicate SPOT packet handler '${next.spotType.name}:${next.packetName ?? next.handlerType.name}'.`
  );
}

function assertUniqueSpotSubscriptionHandler(
  existing: readonly ZLinkSpotSubscriptionHandlerRegistration[] | undefined,
  next: ZLinkSpotSubscriptionHandlerRegistration
): void {
  assertUniqueRegistration(
    existing,
    next,
    (handler) =>
      handler.spotType === next.spotType &&
      handler.topic === next.topic,
    `Duplicate SPOT subscription handler '${next.spotType.name}:${next.topic}'.`
  );
}

function assertUniqueRegistration<TRegistration>(
  existing: readonly TRegistration[] | undefined,
  _next: TRegistration,
  isDuplicate: (handler: TRegistration) => boolean,
  duplicateMessage: string
): void {
  if ((existing ?? []).some(isDuplicate)) {
    throw new framework.ZLinkConfigurationException(duplicateMessage);
  }
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
