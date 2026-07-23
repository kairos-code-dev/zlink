import type { ZLinkLocationOptionOverrides } from '../../contracts/Locations/Options';
import {
  type ZLinkLocationChangeStampStore,
  type ZLinkLocationWatchStore
} from '../../contracts';
import type {
  ZLinkLocationEventSink,
  ZLinkLocationRuntimeStores
} from '../locations';
import {
  ZLinkLocationRuntime,
  ZLinkOwnerLeaseTracker,
  ZLinkStoreLocationResolvers
} from '../locations';
import type { ZLinkFrameworkRegistration } from '../configuration';
import { ZLinkChannelSocketRegistry } from './channel-socket-registry';

export interface ZLinkChannelLocationAutoConnectContext {
  readonly runtime: ZLinkLocationRuntime;
  readonly stores: ZLinkLocationRuntimeStores;
  readonly options: ZLinkLocationOptionOverrides;
  readonly leaseTracker: ZLinkOwnerLeaseTracker;
  readonly resolver: ZLinkStoreLocationResolvers;
  readonly events?: ZLinkLocationEventSink;
  readonly changeStampStore?: ZLinkLocationChangeStampStore;
  readonly watchStore?: ZLinkLocationWatchStore;
}

export function createChannelLocationAutoConnectContext(
  runtime: ZLinkLocationRuntime,
  stores: ZLinkLocationRuntimeStores,
  options: ZLinkLocationOptionOverrides,
  events?: ZLinkLocationEventSink
): ZLinkChannelLocationAutoConnectContext {
  const leaseTracker = new ZLinkOwnerLeaseTracker({
    store: stores.ownerLeaseStore,
    options
  });
  return {
    runtime,
    stores,
    options,
    leaseTracker,
    resolver: new ZLinkStoreLocationResolvers({
      stores,
      leaseTracker,
      events
    }),
    events,
    changeStampStore: isLocationChangeStampStore(stores.peerStore) ? stores.peerStore : undefined,
    watchStore: isLocationWatchStore(stores.peerStore) ? stores.peerStore : undefined
  };
}

export function buildChannelAutoConnectCapabilities(
  _registration: ZLinkFrameworkRegistration,
  _sockets: ZLinkChannelSocketRegistry
): readonly [] {
  return [];
}

function isLocationChangeStampStore(value: unknown): value is ZLinkLocationChangeStampStore {
  return value !== null
    && typeof value === 'object'
    && typeof (value as { getChangeStamp?: unknown }).getChangeStamp === 'function';
}

function isLocationWatchStore(value: unknown): value is ZLinkLocationWatchStore {
  return value !== null
    && typeof value === 'object'
    && typeof (value as { watch?: unknown }).watch === 'function';
}
