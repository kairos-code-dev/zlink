import type { ZLinkFrameworkOptions } from '../../contracts';
export { ZLinkConfigurationException } from './ConfigurationException';
import { createFrameworkOptions } from './RegistrationBuilders';
export { createFrameworkOptions } from './RegistrationBuilders';
import { createCodecRegistry } from './RegistrationCodecRegistry';
import {
  actorFactoriesFromSpotNodes,
  channelNamesWith,
  normalizeLocationRegistration,
  normalizeOptionalPositiveInteger,
  normalizeStreamCompression,
  toChannelMap,
  toRouteChannelOptions,
  toSpotFactorySet,
  toSpotNodeMap,
  toSpotPublisherClientSet,
  toStreamNodeMap
} from './RegistrationNormalizers';
export {
  hasActorManager,
  hasSpotNode,
  hasSpotPublisherClient,
  requirePositiveInteger
} from './RegistrationNormalizers';
import type {
  ZLinkFrameworkRegistration,
  ZLinkFrameworkRegistrationOptions
} from './RegistrationTypes';
export * from './RegistrationTypes';
import { validateFrameworkRegistration } from './RegistrationValidators';
export { validateFrameworkRegistration };

export function createFrameworkRegistration(
  options: ZLinkFrameworkRegistrationOptions = {}
): ZLinkFrameworkRegistration {
  const codecRegistry = createCodecRegistry(options.codecs);
  const routeChannelOptions = toRouteChannelOptions(options);
  const spotNodes = toSpotNodeMap(options.spotNodes);
  const registration: ZLinkFrameworkRegistration = {
    messageSerializers: codecRegistry.registeredSerializers,
    codecs: codecRegistry.registration,
    requestTimeoutMs: normalizeOptionalPositiveInteger(options.requestTimeoutMs, 'requestTimeoutMs'),
    actorFactories: actorFactoriesFromSpotNodes(spotNodes),
    spotFactories: toSpotFactorySet(options.spotFactories, spotNodes),
    channels: toChannelMap(options.channels),
    channelClients: channelNamesWith(options.channels, (channel) => channel.client !== undefined),
    fanoutPublishers: channelNamesWith(options.channels, (channel) => channel.publisher !== undefined),
    routeChannels: new Set(routeChannelOptions.keys()),
    routeChannelOptions,
    streamNodes: toStreamNodeMap(options.streamNodes),
    streamCompression: normalizeStreamCompression(options.streamCompression),
    spotNodes,
    spotPublisherClients: toSpotPublisherClientSet(options.spotPublisherClients, spotNodes),
    filterTypes: [...(options.filters ?? [])],
    worker: options.worker === undefined ? undefined : { ...options.worker },
    dispatch: options.dispatch === undefined ? undefined : { ...options.dispatch },
    monitoring: options.monitoring === undefined ? undefined : { ...options.monitoring },
    locations: normalizeLocationRegistration(options.locations)
  };
  validateFrameworkRegistration(registration, options);
  return registration;
}

export function createFrameworkRegistrationWithBuilder(
  configure: (options: ZLinkFrameworkOptions) => void
): ZLinkFrameworkRegistration {
  return createFrameworkRegistration(createFrameworkOptions(configure));
}
