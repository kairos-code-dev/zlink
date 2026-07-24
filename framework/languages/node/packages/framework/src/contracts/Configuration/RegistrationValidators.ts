import type { Type, ZLinkSpot } from '../../contracts';
import { ZLinkConfigurationException } from './ConfigurationException';
import { requirePositiveInteger } from './RegistrationNormalizers';
import type {
  ZLinkFrameworkRegistration,
  ZLinkFrameworkRegistrationOptions,
  ZLinkRouteChannelOptions,
  ZLinkRouteMeshChannelOptions,
  ZLinkSpotNodeOptions,
  ZLinkSpotPubSubCapabilityOptions,
  ZLinkSpotRouterCapabilityOptions,
  ZLinkSpotRouterPeerConnectionOptions,
  ZLinkWorkerOptions
} from './RegistrationTypes';
import {
  isRouteClientEnabled,
  isRouteTransportDeclared
} from './RouteChannelInternalState';
import { validateTimerRegistration } from './TimerRegistrationValidator';
import { collectRoutingIdAllocationMembers } from './RoutingIdAllocationRegistration';
import { zlinkDefaultLocationOptions } from '../Locations';
import { requireValidSendTimeoutMs } from './SendTimeoutValidation';

export function validateFrameworkRegistration(
  registration: ZLinkFrameworkRegistration,
  options: ZLinkFrameworkRegistrationOptions = {}
): void {
  const actorCapableSpotNodes = [...registration.spotNodes.values()]
    .filter((spotNode) => toActorFactoryCount(spotNode.actorFactories) > 0);
  if (actorCapableSpotNodes.length > 1) {
    throw new ZLinkConfigurationException(
      'Actor factory registration is ambiguous because more than one SpotNode owns actor factories.'
    );
  }

  const peerLocationConfigured = hasLocationStores(registration);
  validateChannelCapabilities(options.channels, peerLocationConfigured);
  validateChannelTopologyNames(registration);
  validateSpotNodes(registration);
  validateRouteChannels(registration, peerLocationConfigured);
  validateStreamNodes(registration);
  validateWorkerOptions(registration.worker);
  validateMonitoring(registration);
  validateLocationRegistration(registration);
  validateClientServerLocationStore(registration);
  validateFanoutLocationStore(registration);
  validateActorTransferAuthority(registration);
  validateRoutingIdAllocations(registration);
}

function validateChannelTopologyNames(registration: ZLinkFrameworkRegistration): void {
  const clientServerChannels = new Set(
    [...registration.channels]
      .filter(([, channel]) => channel.client !== undefined || channel.server !== undefined)
      .map(([channelName]) => channelName)
  );
  if (clientServerChannels.size === 0) return;

  const routeMeshChannels = new Set(registration.routeChannels);
  for (const spotNode of registration.spotNodes.values()) {
    for (const channelName of Object.keys(spotNode.meshChannels ?? {})) {
      routeMeshChannels.add(channelName);
    }
  }
  for (const channelName of clientServerChannels) {
    if (routeMeshChannels.has(channelName)) {
      throw new ZLinkConfigurationException(
        `ChannelName '${channelName}' is registered on both RouteMesh and ClientServer physical paths.`
      );
    }
  }
}

function toActorFactoryCount(value: ZLinkSpotNodeOptions['actorFactories']): number {
  if (value === undefined) {
    return 0;
  }
  return value instanceof Map ? value.size : Object.keys(value).length;
}

function hasLocationStores(registration: ZLinkFrameworkRegistration): boolean {
  return registration.locations.useInMemoryStores
    || registration.locations.storeInstance !== undefined;
}

function validateWorkerOptions(worker: ZLinkWorkerOptions | undefined): void {
  if (worker === undefined) {
    return;
  }
  requirePositiveInteger('Worker maxThreads', worker.maxThreads);
  requirePositiveInteger('Worker maxQueueLength', worker.maxQueueLength);
}

function validateMonitoring(registration: ZLinkFrameworkRegistration): void {
  const monitoring = registration.monitoring;
  if (monitoring === undefined) {
    return;
  }

  validateDuplicateMonitoringSourceNames([
    ...(monitoring.socket ?? []).map((source) => source.sourceName),
    ...(monitoring.spot ?? []).map((source) => source.sourceName),
    ...(monitoring.locationRuntime ?? []).map((source) => source.sourceName),
    ...(monitoring.locationPeer ?? []).map((source) => source.sourceName),
    ...(monitoring.locationSpot ?? []).map((source) => source.sourceName),
    ...(monitoring.locationActor ?? []).map((source) => source.sourceName),
    ...(monitoring.locationRoute ?? []).map((source) => source.sourceName)
  ]);

  const hasLocationMonitoring = (monitoring.locationRuntime?.length ?? 0) > 0
    || (monitoring.locationPeer?.length ?? 0) > 0
    || (monitoring.locationSpot?.length ?? 0) > 0
    || (monitoring.locationActor?.length ?? 0) > 0
    || (monitoring.locationRoute?.length ?? 0) > 0;
  if (hasLocationMonitoring && !hasLocationStores(registration)) {
    throw new ZLinkConfigurationException('Location monitoring requires location stores to be registered.');
  }

  const socketSources = monitoringSocketSources(registration);
  for (const source of monitoring.socket ?? []) {
    requireName('Monitoring socket sourceName', source.sourceName);
    if (!socketSources.has(source.sourceName)) {
      throw new ZLinkConfigurationException(`Monitoring socket source '${source.sourceName}' is not registered.`);
    }
  }

  for (const source of monitoring.spot ?? []) {
    requireName('Monitoring spot sourceName', source.sourceName);
    requirePositiveInteger(`Monitoring spot source '${source.sourceName}' intervalMs`, source.intervalMs);
    if (!registration.spotNodes.has(source.sourceName)) {
      throw new ZLinkConfigurationException(`Monitoring spot source '${source.sourceName}' is not registered.`);
    }
  }

  for (const source of monitoring.locationRuntime ?? []) {
    requireName('Monitoring location runtime sourceName', source.sourceName);
    requirePositiveInteger(`Monitoring location runtime source '${source.sourceName}' intervalMs`, source.intervalMs);
  }

  for (const source of [
    ...(monitoring.locationPeer ?? []),
    ...(monitoring.locationSpot ?? []),
    ...(monitoring.locationActor ?? []),
    ...(monitoring.locationRoute ?? [])
  ]) {
    requireName('Monitoring location sourceName', source.sourceName);
  }
}

function validateLocationRegistration(registration: ZLinkFrameworkRegistration): void {
  const locations = registration.locations;
  if (locations.useInMemoryStores && locations.storeInstance !== undefined) {
    throw new ZLinkConfigurationException(
      'In-memory location stores cannot be combined with explicit location store registrations.'
    );
  }
}

function validateClientServerLocationStore(registration: ZLinkFrameworkRegistration): void {
  const requiresDedicatedStore = [...registration.channels.values()].some((channel) =>
    channel.server !== undefined
    || (channel.client !== undefined && (channel.client.manualConnections?.length ?? 0) === 0));
  if (!requiresDedicatedStore || registration.locations.useInMemoryStores) return;
  const store = registration.locations.storeInstance as Partial<Record<
    'updateClientServer' | 'removeClientServer' | 'listClientServers',
    unknown
  >> | undefined;
  if (store === undefined) return;
  if (typeof store.updateClientServer !== 'function'
    || typeof store.removeClientServer !== 'function'
    || typeof store.listClientServers !== 'function') {
    throw new ZLinkConfigurationException(
      'ClientServer automatic discovery requires dedicated ClientServer descriptor store operations.'
    );
  }
}

function validateFanoutLocationStore(registration: ZLinkFrameworkRegistration): void {
  const hasStore = hasLocationStores(registration);
  const requiresDedicatedStore = [...registration.channels.values()].some((channel) =>
    (channel.publisher !== undefined && hasStore)
    || (channel.subscriber !== undefined
      && (channel.subscriber.manualConnections?.length ?? 0) === 0));
  if (!requiresDedicatedStore || registration.locations.useInMemoryStores) return;
  const store = registration.locations.storeInstance as Partial<Record<
    'updateFanoutPublisher' | 'removeFanoutPublisher' | 'listFanoutPublishers',
    unknown
  >> | undefined;
  if (store === undefined) return;
  if (typeof store.updateFanoutPublisher !== 'function'
    || typeof store.removeFanoutPublisher !== 'function'
    || typeof store.listFanoutPublishers !== 'function') {
    throw new ZLinkConfigurationException(
      'Classic fanout automatic discovery requires dedicated fanout publisher descriptor store operations.'
    );
  }
}

function validateActorTransferAuthority(registration: ZLinkFrameworkRegistration): void {
  if (registration.actorTransferAdapters.size === 0) {
    return;
  }
  const store = registration.locations.storeInstance as {
    prepareActorTransfer?: unknown;
    commitActorTransfer?: unknown;
    activateActorTransfer?: unknown;
    abortActorTransfer?: unknown;
    takeOverActorTransfer?: unknown;
    resolveActorTransfer?: unknown;
  } | undefined;
  if (
    registration.locations.useInMemoryStores
    || store === undefined
    || typeof store.prepareActorTransfer !== 'function'
    || typeof store.commitActorTransfer !== 'function'
    || typeof store.activateActorTransfer !== 'function'
    || typeof store.abortActorTransfer !== 'function'
    || typeof store.takeOverActorTransfer !== 'function'
    || typeof store.resolveActorTransfer !== 'function'
  ) {
    throw new ZLinkConfigurationException(
      'Actor transfer adapters require a durable location store with Actor transfer authority.'
    );
  }
}

function validateRoutingIdAllocations(registration: ZLinkFrameworkRegistration): void {
  const members = collectRoutingIdAllocationMembers(registration);
  if (members.length === 0) return;
  if (!hasLocationStores(registration)) {
    throw new ZLinkConfigurationException(
      'Allocated routing ids require a location store or in-memory location stores.'
    );
  }
  const explicitStore = registration.locations.storeInstance;
  if (explicitStore !== undefined && !isRoutingIdAllocationStore(explicitStore)) {
    throw new ZLinkConfigurationException(
      'The registered location store does not provide routing-id slot allocation.'
    );
  }

  const options = { ...zlinkDefaultLocationOptions, ...registration.locations.options };
  const times = [
    options.heartbeatIntervalMs,
    options.ownerLeaseTtlMs,
    options.routingIdFencingMarginMs,
    options.ownerLeaseRenewTimeoutMs
  ];
  if (times.some((value) => !Number.isFinite(value) || value <= 0)) {
    throw new ZLinkConfigurationException('Allocated routing-id lease times must be greater than zero.');
  }
  if (options.heartbeatIntervalMs + options.ownerLeaseRenewTimeoutMs
      >= options.ownerLeaseTtlMs - options.routingIdFencingMarginMs) {
    throw new ZLinkConfigurationException(
      'Allocated routing-id lease times must satisfy heartbeat + renew timeout < TTL - fencing margin.'
    );
  }

  const groups = new Map<string, typeof members>();
  for (const member of members) {
    if (!Number.isInteger(member.slotCount) || member.slotCount < 1) {
      throw new ZLinkConfigurationException(
        `Routing-id allocation group '${member.groupName}' must configure at least one slot.`
      );
    }
    if (member.groupName.trim().length === 0 || member.routingIdPrefix.trim().length === 0) {
      throw new ZLinkConfigurationException('Routing-id allocation names and prefixes must not be empty.');
    }
    if (Buffer.byteLength(`${member.routingIdPrefix}${member.slotCount}`, 'utf8') > 255) {
      throw new ZLinkConfigurationException(
        `Routing-id allocation member '${member.memberName}' can exceed the 255 byte routing-id limit.`
      );
    }
    if (member.fixedRoutingId !== undefined) {
      throw new ZLinkConfigurationException(
        `Routing-id allocation member '${member.memberName}' cannot combine fixed and allocated routing ids.`
      );
    }
    if (member.explicitEntrySpotRoutingId) {
      throw new ZLinkConfigurationException(
        `SpotNode '${member.memberName}' cannot combine allocated routing id and explicit Entry Spot routing id.`
      );
    }
    if (!member.hasBindableRole) {
      throw new ZLinkConfigurationException(
        `Routing-id allocation member '${member.memberName}' must enable a channel or SpotNode role.`
      );
    }
    const group = groups.get(member.groupName) ?? [];
    groups.set(member.groupName, [...group, member]);
  }
  for (const [groupName, group] of groups) {
    if (group.some((member) => member.slotCount !== group[0]?.slotCount)) {
      throw new ZLinkConfigurationException(
        `Routing-id allocation group '${groupName}' must use one slot count for every member.`
      );
    }
    if (new Set(group.map((member) => member.memberName)).size !== group.length) {
      throw new ZLinkConfigurationException(
        `Routing-id allocation group '${groupName}' contains duplicate members.`
      );
    }
  }
}

function isRoutingIdAllocationStore(value: unknown): boolean {
  const store = value as {
    acquireRoutingIdSlot?: unknown;
    releaseRoutingIdSlot?: unknown;
    listRoutingIdSlots?: unknown;
  };
  return typeof store.acquireRoutingIdSlot === 'function'
    && typeof store.releaseRoutingIdSlot === 'function'
    && typeof store.listRoutingIdSlots === 'function';
}

function validateDuplicateMonitoringSourceNames(sourceNames: readonly string[]): void {
  const seen = new Set<string>();
  for (const sourceName of sourceNames) {
    requireName('Monitoring sourceName', sourceName);
    if (seen.has(sourceName)) {
      throw new ZLinkConfigurationException(`Duplicate monitoring source '${sourceName}'.`);
    }
    seen.add(sourceName);
  }
}

function monitoringSocketSources(registration: ZLinkFrameworkRegistration): ReadonlySet<string> {
  const sources = new Set<string>();
  for (const [channelName, channel] of registration.channels.entries()) {
    if (channel.server !== undefined) {
      sources.add(`${channelName}.server`);
    }
    if (channel.client !== undefined) {
      sources.add(`${channelName}.client`);
    }
    if (channel.publisher !== undefined) {
      sources.add(`${channelName}.publisher`);
    }
    if (channel.subscriber !== undefined) {
      sources.add(`${channelName}.subscriber`);
    }
  }
  for (const [routerChannelId, routeChannel] of registration.routeChannelOptions.entries()) {
    if (hasBind(routeChannel.bind)) {
      sources.add(`${routerChannelId}.router`);
    }
    if (isRouteClientEnabled(routeChannel) || (routeChannel.manualConnections ?? []).length > 0) {
      sources.add(`${routerChannelId}.client`);
    }
  }
  return sources;
}

function requireNonNegativeInteger(label: string, value: number | undefined): void {
  if (value === undefined) {
    return;
  }
  if (!Number.isInteger(value) || value < 0) {
    throw new ZLinkConfigurationException(`${label} must be a non-negative integer.`);
  }
}

function validateChannelCapabilities(
  channels: ZLinkFrameworkRegistrationOptions['channels'],
  peerLocationConfigured: boolean
): void {
  for (const [channelName, channel] of Object.entries(channels ?? {})) {
    if (channel.server !== undefined) {
      requireEndpoint(`channel '${channelName}' server`, channel.server.bind);
      if (channel.server.routingId !== undefined) {
        requireName(`channel '${channelName}' server routingId`, channel.server.routingId);
      }
      requirePeerWeight(`channel '${channelName}' server weight`, channel.server.weight);
      requireSocketOptions(`channel '${channelName}' server`, channel.server);
    }
    if (channel.publisher !== undefined) {
      requireEndpoint(`channel '${channelName}' publisher`, channel.publisher.bind);
    }
    if (channel.client !== undefined) {
      requirePeerSource(
        `channel '${channelName}' client`,
        channel.client.manualConnections,
        peerLocationConfigured || channel.server !== undefined
      );
      requireSocketOptions(`channel '${channelName}' client`, channel.client);
    }
    if (channel.subscriber !== undefined) {
      requirePeerSource(`channel '${channelName}' subscriber`, channel.subscriber.manualConnections, peerLocationConfigured);
    }
    if ((channel.publishHandlers ?? []).length > 0 && channel.subscriber === undefined) {
      throw new ZLinkConfigurationException(
        `Channel '${channelName}' publish handlers require a subscriber capability.`
      );
    }
    if (channel.subscriber !== undefined && (channel.publishHandlers ?? []).length === 0) {
      throw new ZLinkConfigurationException(
        `Channel '${channelName}' subscriber must register at least one publish handler.`
      );
    }
    if (((channel.requestHandlers ?? []).length > 0 || (channel.sendHandlers ?? []).length > 0) && channel.server === undefined) {
      throw new ZLinkConfigurationException(
        `Channel '${channelName}' request/send handlers require a server capability.`
      );
    }
    if (
      channel.server !== undefined
      && (channel.requestHandlers ?? []).length + (channel.sendHandlers ?? []).length === 0
    ) {
      throw new ZLinkConfigurationException(
        `Channel '${channelName}' server must register at least one request or send handler.`
      );
    }
    validateDuplicatePacketNames(
      `channel '${channelName}' request handler`,
      channel.requestHandlers?.map((handler) => handler.packetName)
    );
    validateDuplicatePacketNames(
      `channel '${channelName}' send handler`,
      channel.sendHandlers?.map((handler) => handler.packetName)
    );
    validateDuplicatePacketNames(
      `channel '${channelName}' publish handler`,
      channel.publishHandlers?.map((handler) => handler.packetName)
    );
    if (channel.routeMesh !== undefined) {
      validateRouteMeshCapability(
        `channel '${channelName}' route mesh`,
        channel.routeMesh,
        peerLocationConfigured);
    }
  }
}

function validateSpotNodes(registration: ZLinkFrameworkRegistration): void {
  for (const [spotNodeName, spotNode] of registration.spotNodes.entries()) {
    if (spotNodeName.trim().length === 0 || spotNodeName.trim() !== spotNodeName) {
      throw new ZLinkConfigurationException('SpotNode name must not be empty or padded.');
    }
    if (spotNode.router === undefined && spotNode.pubSub === undefined) {
      throw new ZLinkConfigurationException(
        `SpotNode '${spotNodeName}' must enable router or pubSub capability.`
      );
    }
    if (toActorFactoryCount(spotNode.actorFactories) > 0 && spotNode.router === undefined) {
      throw new ZLinkConfigurationException(
        `SpotNode '${spotNodeName}' must enable router capability when actor factories are registered.`
      );
    }
    validateSpotNodeCapability(`SpotNode '${spotNodeName}' router`, spotNode.router);
    validateSpotNodeCapability(`SpotNode '${spotNodeName}' pubSub`, spotNode.pubSub);
    requireValidSendTimeoutMs(
      `SpotNode '${spotNodeName}' publisher sendTimeoutMs`,
      spotNode.publisherConfig?.sendTimeoutMs
    );
    validateEntrySpot(spotNodeName, spotNode);
    validateSpotNodeFactories(spotNodeName, spotNode);
    validateSpotNodeTimers(spotNode);
    if (
      spotNode.router?.routingId !== undefined &&
      spotNode.pubSub?.routingId !== undefined &&
      spotNode.router.routingId !== spotNode.pubSub.routingId
    ) {
      throw new ZLinkConfigurationException(
        `SpotNode '${spotNodeName}' router and pubSub routingId must match.`
      );
    }
  }
}

function validateSpotNodeTimers(spotNode: ZLinkSpotNodeOptions): void {
  for (const timer of spotNode.entrySpotTimerHandlers ?? []) {
    validateTimerRegistration(timer.name, timer.periodMs, timer.options);
  }
  for (const timer of spotNode.spotTimerHandlers ?? []) {
    validateTimerRegistration(timer.name, timer.periodMs, timer.options);
  }
}

function validateEntrySpot(spotNodeName: string, spotNode: ZLinkSpotNodeOptions): void {
  if (spotNode.entrySpot?.routingId !== undefined) {
    requireName(`SpotNode '${spotNodeName}' Entry Spot routingId`, spotNode.entrySpot.routingId);
  }
}

function validateSpotNodeFactories(spotNodeName: string, spotNode: ZLinkSpotNodeOptions): void {
  const seen = new Set<Type<ZLinkSpot>>();
  for (const factory of spotNode.spotFactories ?? []) {
    if (seen.has(factory)) {
      throw new ZLinkConfigurationException(
        `Duplicate SPOT factory registration on SpotNode '${spotNodeName}'.`
      );
    }
    seen.add(factory);
  }
}

function validateSpotNodeCapability(
  capabilityName: string,
  capability: ZLinkSpotRouterCapabilityOptions | ZLinkSpotPubSubCapabilityOptions | undefined
): void {
  if (capability === undefined) {
    return;
  }
  validateManualConnections(capabilityName, capability.manualConnections);
  validateRouterPeerConnections(capabilityName, 'manualPeerConnections' in capability ? capability.manualPeerConnections : undefined);
  requireEndpoint(capabilityName, capability.bind);
  if (capability.routingId !== undefined && (capability.routingId.trim().length === 0 || capability.routingId.trim() !== capability.routingId)) {
    throw new ZLinkConfigurationException(`${capabilityName} routingId must not be empty or padded.`);
  }
}

function validateManualConnections(capabilityName: string, manualConnections: readonly string[] | undefined): void {
  if ((manualConnections ?? []).some((endpoint) => endpoint.trim().length === 0)) {
    throw new ZLinkConfigurationException(`${capabilityName} manual connection endpoint must not be empty.`);
  }
}

function validateRouterPeerConnections(
  capabilityName: string,
  manualPeerConnections: readonly ZLinkSpotRouterPeerConnectionOptions[] | undefined
): void {
  const seenPeerRids = new Set<string>();
  for (const connection of manualPeerConnections ?? []) {
    const peerRid = String(connection.peerRid);
    if (peerRid.trim().length === 0) {
      throw new ZLinkConfigurationException(`${capabilityName} manual peer routing id must not be empty.`);
    }
    if (seenPeerRids.has(peerRid)) {
      throw new ZLinkConfigurationException(`${capabilityName} manual peer routing id must be unique.`);
    }
    seenPeerRids.add(peerRid);
    if (connection.endpoint.trim().length === 0) {
      throw new ZLinkConfigurationException(`${capabilityName} manual peer connection endpoint must not be empty.`);
    }
  }
}

function validateDuplicatePacketNames(label: string, packetNames: readonly string[] | undefined): void {
  const seen = new Set<string>();
  for (const packetName of packetNames ?? []) {
    requireName(label, packetName);
    if (seen.has(packetName)) {
      throw new ZLinkConfigurationException(`Duplicate ${label} packet '${packetName}'.`);
    }
    seen.add(packetName);
  }
}

function requireName(label: string, value: string): void {
  if (value.trim().length === 0 || value.trim() !== value) {
    throw new ZLinkConfigurationException(`${label} must not be empty or padded.`);
  }
}

function requirePeerSource(
  capabilityName: string,
  manualConnections: readonly string[] | undefined,
  peerLocationConfigured: boolean
): void {
  validateManualConnections(capabilityName, manualConnections);
  if ((manualConnections ?? []).length > 0 || peerLocationConfigured) {
    return;
  }
  throw new ZLinkConfigurationException(`${capabilityName} requires location stores or manual connections.`);
}

function requireEndpoint(capabilityName: string, endpoint: string | undefined): void {
  if (endpoint === undefined || endpoint.trim().length === 0) {
    throw new ZLinkConfigurationException(`${capabilityName} must define a bind endpoint.`);
  }
}

function requireFilePath(label: string, value: string | undefined): void {
  if (value === undefined || value.trim().length === 0) {
    throw new ZLinkConfigurationException(`${label} must define a file path.`);
  }
}

function validateStreamNodes(registration: ZLinkFrameworkRegistration): void {
  for (const [streamNodeName, streamNode] of registration.streamNodes.entries()) {
    requireEndpoint(`STREAM node '${streamNodeName}'`, streamNode.bind);
    if (streamNode.tlsServer !== undefined) {
      requireFilePath(`STREAM node '${streamNodeName}' TLS certificate`, streamNode.tlsServer.certificatePath);
      requireFilePath(`STREAM node '${streamNodeName}' TLS key`, streamNode.tlsServer.keyPath);
    }
    if (streamNode.session === undefined) {
      throw new ZLinkConfigurationException(
        `STREAM node '${streamNodeName}' must register a header stream session.`
      );
    }
  }
}

function validateRouteChannels(registration: ZLinkFrameworkRegistration, peerLocationConfigured: boolean): void {
  for (const routeChannel of registration.routeChannelOptions.values()) {
    if (!isRouteTransportDeclared(routeChannel) && !isRouteTransportConfigured(routeChannel)) {
      continue;
    }
    if (
      !isRoutePacketCapabilityConfigured(routeChannel) &&
      isAcceptedSpotRouteChannel(registration, routeChannel)
    ) {
      continue;
    }
    validateRouteMeshCapability(`route channel '${routeChannel.routerChannelId}'`, routeChannel, peerLocationConfigured);
    if (routeChannelHandlerCount(routeChannel) > 0 && !hasBind(routeChannel.bind)) {
      requireEndpoint(`route channel '${routeChannel.routerChannelId}' router`, routeChannel.bind);
    }
  }
}

function isRouteTransportConfigured(routeChannel: ZLinkRouteChannelOptions): boolean {
  return isRoutePacketCapabilityConfigured(routeChannel);
}

function isRoutePacketCapabilityConfigured(routeChannel: ZLinkRouteChannelOptions): boolean {
  return hasBind(routeChannel.bind)
    || isRouteClientEnabled(routeChannel)
    || (routeChannel.manualConnections ?? []).length > 0
    || routeChannelHandlerCount(routeChannel) > 0;
}

function isAcceptedSpotRouteChannel(
  registration: ZLinkFrameworkRegistration,
  routeChannel: ZLinkRouteChannelOptions
): boolean {
  if (!isRouteTransportDeclared(routeChannel)) {
    return false;
  }
  if (registration.spotNodes.get(routeChannel.routerChannelId)?.router !== undefined) {
    return true;
  }
  if (routeChannel.routingId !== undefined) {
    return [...registration.spotNodes.values()].some((spotNode) =>
      spotNode.router?.routingId === routeChannel.routingId);
  }
  return [...registration.spotNodes.values()].filter((spotNode) => spotNode.router !== undefined).length === 1;
}

function validateRouteMeshCapability(
  capabilityName: string,
  routeChannel: ZLinkRouteChannelOptions | ZLinkRouteMeshChannelOptions,
  peerLocationConfigured = false
): void {
  const clientEnabled = isRouteClientEnabled(routeChannel)
    || (routeChannel.manualConnections ?? []).length > 0;
  if (!hasBind(routeChannel.bind) && !clientEnabled) {
    throw new ZLinkConfigurationException(`${capabilityName} must enable server or client capability.`);
  }
  if (clientEnabled) {
    requirePeerSource(capabilityName, routeChannel.manualConnections, peerLocationConfigured);
  }
  requirePeerWeight(`${capabilityName} weight`, routeChannel.weight);
  requireSocketOptions(capabilityName, routeChannel);
}

function routeChannelHandlerCount(routeChannel: ZLinkRouteChannelOptions): number {
  return (routeChannel.handlers ?? []).length +
    (routeChannel.sendHandlers ?? []).length +
    (routeChannel.requestHandlers ?? []).length;
}

function hasBind(endpoint: string | undefined): boolean {
  return endpoint !== undefined && endpoint.trim().length > 0;
}

function requirePeerWeight(label: string, value: number | undefined): void {
  if (value === undefined) {
    return;
  }
  if (!Number.isInteger(value) || value < 0 || value > 100) {
    throw new ZLinkConfigurationException(`${label} must be between 0 and 100.`);
  }
}

function requireSocketOptions(
  label: string,
  options: {
    readonly sendHighWaterMark?: number;
    readonly receiveHighWaterMark?: number;
    readonly sendTimeoutMs?: number;
    readonly maxMessageSize?: number;
  }
): void {
  requireNonNegativeInteger(`${label} sendHighWaterMark`, options.sendHighWaterMark);
  requireNonNegativeInteger(`${label} receiveHighWaterMark`, options.receiveHighWaterMark);
  requireNonNegativeInteger(`${label} maxMessageSize`, options.maxMessageSize);
  requireValidSendTimeoutMs(`${label} sendTimeoutMs`, options.sendTimeoutMs);
}
