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
    ...(monitoring.locationRuntime ?? []).map((source) => source.sourceName)
  ]);

  const hasLocationMonitoring = (monitoring.locationRuntime?.length ?? 0) > 0;
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

  for (const source of monitoring.locationRuntime ?? []) {
    requireName('Monitoring location runtime sourceName', source.sourceName);
    requirePositiveInteger(`Monitoring location runtime source '${source.sourceName}' intervalMs`, source.intervalMs);
  }
}

function validateLocationRegistration(registration: ZLinkFrameworkRegistration): void {
  const locations = registration.locations;
  if (locations.useInMemoryStores && locations.storeInstance !== undefined) {
    throw new ZLinkConfigurationException(
      'In-memory location stores cannot be combined with explicit location store registrations.'
    );
  }
  const store = locations.storeInstance;
  if (store !== undefined
    && (typeof store.read !== 'function'
      || typeof store.write !== 'function'
      || typeof store.scan !== 'function')) {
    throw new ZLinkConfigurationException(
      'Location Store must implement read, write, and scan.'
    );
  }
  const options = { ...zlinkDefaultLocationOptions, ...locations.options };
  for (const [name, value] of Object.entries({
    routeCacheMaxAgeMs: options.routeCacheMaxAgeMs,
    relocationForwardingWindowMs: options.relocationForwardingWindowMs
  })) {
    if (!Number.isFinite(value) || value < 0) {
      throw new ZLinkConfigurationException(`${name} must be a finite non-negative number.`);
    }
  }
  if (
    options.routeCacheMaxAgeMs > 0
    && options.relocationForwardingWindowMs > 0
    && options.routeCacheMaxAgeMs > options.relocationForwardingWindowMs - 5000
  ) {
    throw new ZLinkConfigurationException(
      'routeCacheMaxAgeMs must be at least 5000 ms shorter than relocationForwardingWindowMs.'
    );
  }
  for (const [name, value] of Object.entries({
    maxActiveOutboundRelocations: options.maxActiveOutboundRelocations,
    maxActiveInboundRelocations: options.maxActiveInboundRelocations,
    maxConcurrentRelocationCaptures: options.maxConcurrentRelocationCaptures,
    maxConcurrentRelocationRestores: options.maxConcurrentRelocationRestores,
    maxRelocationPayloadInFlightBytes: options.maxRelocationPayloadInFlightBytes
  })) {
    if (!Number.isSafeInteger(value) || value <= 0) {
      throw new ZLinkConfigurationException(`${name} must be a positive safe integer.`);
    }
  }
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
  if (!Number.isInteger(value) || value < 0 || value > 10_000) {
    throw new ZLinkConfigurationException(`${label} must be an integer in 0..10000.`);
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
