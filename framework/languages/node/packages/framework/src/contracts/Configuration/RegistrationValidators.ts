import type { Type, ZLinkSpot } from '../../contracts';
import {
  ZLinkConfigurationException,
  requirePositiveInteger,
  type ZLinkChannelOptions,
  type ZLinkDiscoveryOptions,
  type ZLinkFrameworkRegistration,
  type ZLinkFrameworkRegistrationOptions,
  type ZLinkRouteChannelOptions,
  type ZLinkSpotNodeOptions,
  type ZLinkSpotPubSubCapabilityOptions,
  type ZLinkSpotRouterCapabilityOptions,
  type ZLinkSpotRouterPeerConnectionOptions,
  type ZLinkWorkerOptions
} from './Registration';

export function validateFrameworkRegistration(
  registration: ZLinkFrameworkRegistration,
  options: ZLinkFrameworkRegistrationOptions = {}
): void {
  if (registration.actorFactories.size > 0 && registration.spotNodes.size === 0) {
    throw new ZLinkConfigurationException('Actor factory registration requires at least one SpotNode.');
  }

  if (registration.hasRegistrySpotRemoteAddresses && registration.routeChannels.size === 0) {
    throw new ZLinkConfigurationException('Registry remote address resolver requires a route mesh channel.');
  }

  if (registration.hasRegistrySpotRemoteAddresses && registration.hasSpotRemoteAddressResolver) {
    throw new ZLinkConfigurationException('SPOT remote address resolver is already registered.');
  }

  if (registration.hasRegistrySpotRemoteAddresses && !hasDiscovery(options.discovery)) {
    throw new ZLinkConfigurationException(
      'Registry remote address resolver requires discovery endpoints from discovery.registries.'
    );
  }

  validateRegistryRouteChannel(registration);
  validateChannelCapabilities(options.channels, hasDiscovery(options.discovery));
  validateSpotNodes(registration);
  validateRouteChannels(registration.routeChannelOptions);
  validateStreamNodes(registration);
  validateWorkerOptions(registration.worker);
}

export function hasDiscovery(discovery: ZLinkDiscoveryOptions | undefined): boolean {
  return (discovery?.registries ?? []).some((endpoint) => endpoint.trim().length > 0);
}

function validateWorkerOptions(worker: ZLinkWorkerOptions | undefined): void {
  if (worker === undefined) {
    return;
  }
  requireNonNegativeInteger('Worker minThreads', worker.minThreads);
  requirePositiveInteger('Worker maxThreads', worker.maxThreads);
  requireNonNegativeInteger('Worker idleTimeoutMs', worker.idleTimeoutMs);
  requirePositiveInteger('Worker maxQueueLength', worker.maxQueueLength);
  if (
    worker.minThreads !== undefined
    && worker.maxThreads !== undefined
    && worker.minThreads > worker.maxThreads
  ) {
    throw new ZLinkConfigurationException('Worker minThreads must not exceed maxThreads.');
  }
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
  discoveryConfigured: boolean
): void {
  for (const [channelName, channel] of Object.entries(channels ?? {})) {
    if (channel.server !== undefined) {
      requireEndpoint(`channel '${channelName}' server`, channel.server.bind);
      if (channel.server.routingId !== undefined) {
        requireName(`channel '${channelName}' server routingId`, channel.server.routingId);
      }
    }
    if (channel.publisher !== undefined) {
      requireEndpoint(`channel '${channelName}' publisher`, channel.publisher.bind);
    }
    if (channel.client !== undefined) {
      requirePeerSource(`channel '${channelName}' client`, channel.client.manualConnections, discoveryConfigured);
    }
    if (channel.subscriber !== undefined) {
      requirePeerSource(`channel '${channelName}' subscriber`, channel.subscriber.manualConnections, discoveryConfigured);
    }
    if ((channel.publishHandlers ?? []).length > 0 && channel.subscriber === undefined) {
      throw new ZLinkConfigurationException(
        `Channel '${channelName}' publish handlers require a subscriber capability.`
      );
    }
    if (((channel.requestHandlers ?? []).length > 0 || (channel.sendHandlers ?? []).length > 0) && channel.server === undefined) {
      throw new ZLinkConfigurationException(
        `Channel '${channelName}' request/send handlers require a server capability.`
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
      requireEndpoint(`channel '${channelName}' route mesh`, channel.routeMesh.bind);
      validateManualConnections(`channel '${channelName}' route mesh`, channel.routeMesh.manualConnections);
    }
  }
}

function validateSpotNodes(registration: ZLinkFrameworkRegistration): void {
  for (const [spotNodeName, spotNode] of registration.spotNodes.entries()) {
    if (spotNodeName.trim().length === 0 || spotNodeName.trim() !== spotNodeName) {
      throw new ZLinkConfigurationException('SpotNode name must not be empty or padded.');
    }
    validateSpotNodeCapability(`SpotNode '${spotNodeName}' router`, spotNode.router);
    validateSpotNodeCapability(`SpotNode '${spotNodeName}' pubSub`, spotNode.pubSub);
    validateEntrySpot(spotNodeName, spotNode);
    validateSpotNodeFactories(spotNodeName, spotNode);
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
  if (capability.bind !== undefined) {
    requireEndpoint(capabilityName, capability.bind);
  }
  validateManualConnections(capabilityName, capability.manualConnections);
  validateRouterPeerConnections(capabilityName, 'manualPeerConnections' in capability ? capability.manualPeerConnections : undefined);
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
  for (const connection of manualPeerConnections ?? []) {
    if (String(connection.peerRid).trim().length === 0) {
      throw new ZLinkConfigurationException(`${capabilityName} manual peer routing id must not be empty.`);
    }
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

function validateRegistryRouteChannel(registration: ZLinkFrameworkRegistration): void {
  if (!registration.hasRegistrySpotRemoteAddresses) {
    return;
  }

  const routerChannelId = registration.registrySpotRemoteAddresses?.routerChannelId;
  if (routerChannelId !== undefined) {
    if (!registration.routeChannels.has(routerChannelId)
      && registration.spotNodes.get(routerChannelId)?.router === undefined) {
      throw new ZLinkConfigurationException(
        `Registry SPOT remote address resolver references unknown route mesh channel or router-capable SpotNode '${routerChannelId}'.`
      );
    }
    return;
  }

  const routerSpotNodeCount = [...registration.spotNodes.values()]
    .filter((spotNode) => spotNode.router !== undefined)
    .length;
  if (registration.routeChannels.size + routerSpotNodeCount !== 1) {
    throw new ZLinkConfigurationException(
      'Registry SPOT remote address resolver requires RouterChannelId when route mesh channel or router-capable SpotNode is ambiguous.'
    );
  }
}

function requirePeerSource(
  capabilityName: string,
  manualConnections: readonly string[] | undefined,
  discoveryConfigured: boolean
): void {
  validateManualConnections(capabilityName, manualConnections);
  if ((manualConnections ?? []).length > 0 || discoveryConfigured) {
    return;
  }
  throw new ZLinkConfigurationException(`${capabilityName} requires discovery or manual connections.`);
}

function requireEndpoint(capabilityName: string, endpoint: string | undefined): void {
  if (endpoint === undefined || endpoint.trim().length === 0) {
    throw new ZLinkConfigurationException(`${capabilityName} must define a bind endpoint.`);
  }
}

function validateStreamNodes(registration: ZLinkFrameworkRegistration): void {
  for (const [streamNodeName, streamNode] of registration.streamNodes.entries()) {
    requireEndpoint(`STREAM node '${streamNodeName}'`, streamNode.bind);
    if (streamNode.session === undefined) {
      throw new ZLinkConfigurationException(
        `STREAM node '${streamNodeName}' must register a header stream session.`
      );
    }
  }
}

function validateRouteChannels(routeChannels: ReadonlyMap<string, ZLinkRouteChannelOptions>): void {
  for (const routeChannel of routeChannels.values()) {
    if (routeChannelHandlerCount(routeChannel) > 0) {
      requireEndpoint(`route channel '${routeChannel.routerChannelId}' router`, routeChannel.bind);
    }
  }
}

function routeChannelHandlerCount(routeChannel: ZLinkRouteChannelOptions): number {
  return (routeChannel.handlers ?? []).length +
    (routeChannel.sendHandlers ?? []).length +
    (routeChannel.requestHandlers ?? []).length;
}
