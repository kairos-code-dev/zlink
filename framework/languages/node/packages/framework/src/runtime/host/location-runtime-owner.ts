import type {
  RoutingId,
  ZLinkRoutingIdSlotAllocationStore,
  ZLinkRuntimeEventPublisher
} from '../../contracts';
import type { ZLinkFrameworkRegistration } from '../configuration';
import type { ZLinkChannelRuntimeManager } from '../channels';
import { ZLinkLocationMonitoringEventEmitter } from '../diagnostics';
import {
  ZLinkInMemoryLocationStore,
  ZLinkLocationLifecycle,
  ZLinkLocationRuntime,
  ZLinkLocationSpotRouteResolver,
  ZLinkOwnerLeaseTracker,
  ZLinkStoreLocationResolvers,
  type ZLinkLocationEventSink,
  type ZLinkLocationRuntimeStores
} from '../locations';
import type { ZLinkSpotNodeRuntimeManager } from '../spots';
import type { ZLinkSpotRouteResolver } from '../spots/spot-routing-internal';

export interface ZLinkLocationRuntimeOwnerOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly runtimeEventPublisher: ZLinkRuntimeEventPublisher;
  readonly fallbackNodeRid: RoutingId;
  readonly metrics: import('../diagnostics').ZLinkRuntimeMetrics;
  readonly allocatedRoutingIdsEnabled: boolean;
}

export interface ZLinkLocationRuntimeStopSnapshot {
  readonly runtime?: ZLinkLocationRuntime;
  readonly lifecycle?: ZLinkLocationLifecycle;
}

export class ZLinkLocationRuntimeOwner {
  private stores?: ZLinkLocationRuntimeStores;
  private runtime?: ZLinkLocationRuntime;
  private lifecycle?: ZLinkLocationLifecycle;
  private events?: ZLinkLocationEventSink;
  private leaseTrackerState?: {
    readonly stores: ZLinkLocationRuntimeStores;
    readonly tracker: ZLinkOwnerLeaseTracker;
  };

  constructor(private readonly options: ZLinkLocationRuntimeOwnerOptions) {}

  get currentStores(): ZLinkLocationRuntimeStores | undefined {
    return this.stores;
  }

  get currentRuntime(): ZLinkLocationRuntime | undefined {
    return this.runtime;
  }

  get currentLifecycle(): ZLinkLocationLifecycle | undefined {
    return this.lifecycle;
  }

  get currentEvents(): ZLinkLocationEventSink | undefined {
    return this.events;
  }

  routingIdAllocationStore(): ZLinkRoutingIdSlotAllocationStore | undefined {
    const store = this.stores?.locationStore ?? this.createAndRememberStores()?.locationStore;
    return isRoutingIdAllocationStore(store) ? store : undefined;
  }

  ensureRuntime(primarySpotMeshName: string | undefined): ZLinkLocationRuntime | undefined {
    if (this.runtime !== undefined) {
      return this.runtime;
    }
    const stores = this.stores ?? this.createRuntimeStores();
    if (stores === undefined) {
      return undefined;
    }
    this.stores = stores;
    const runtime = new ZLinkLocationRuntime({
      stores,
      options: this.options.registration.locations.options,
      events: this.ensureEvents(),
      leaseTracker: this.leaseTracker(stores),
      metrics: this.options.metrics,
      allocatedRoutingIdsEnabled: this.options.allocatedRoutingIdsEnabled
    });
    this.runtime = runtime;
    this.lifecycle = new ZLinkLocationLifecycle(runtime, stores.actorStore, primarySpotMeshName ?? '');
    return runtime;
  }

  createRefResolver(spotMeshNames: readonly string[]): ZLinkStoreLocationResolvers | undefined {
    this.ensureRuntime(spotMeshNames.length === 1 ? spotMeshNames[0] : undefined);
    const stores = this.stores;
    if (stores === undefined) {
      return undefined;
    }
    return new ZLinkStoreLocationResolvers({
      stores,
      leaseTracker: this.leaseTracker(stores),
      events: this.events,
      spotMeshNames
    });
  }

  createSpotRouteResolver(
    spotMeshNames: readonly string[],
    spotRouterChannelIdByMesh: (meshName: string) => string
  ): ZLinkSpotRouteResolver | undefined {
    const stores = this.stores;
    if (stores === undefined || spotMeshNames.length === 0) {
      return undefined;
    }
    return new ZLinkLocationSpotRouteResolver(
      new ZLinkStoreLocationResolvers({
        stores,
        leaseTracker: this.leaseTracker(stores),
        events: this.events
      }),
      spotMeshNames,
      spotRouterChannelIdByMesh
    );
  }

  createActorLocationResolver(): ZLinkStoreLocationResolvers | undefined {
    const stores = this.stores;
    if (stores === undefined) {
      return undefined;
    }
    return new ZLinkStoreLocationResolvers({
      stores,
      leaseTracker: this.leaseTracker(stores),
      events: this.events
    });
  }

  ownerNodeRid(spotNodeRuntime?: ZLinkSpotNodeRuntimeManager): RoutingId {
    const spotNodeRid = spotNodeRuntime?.primaryNode?.routingId;
    if (spotNodeRid !== undefined) {
      return spotNodeRid;
    }
    for (const channel of this.options.registration.channels.values()) {
      const routingId = channel.routingId ?? channel.server?.routingId;
      if (routingId !== undefined) {
        return routingId;
      }
    }
    for (const routeChannel of this.options.registration.routeChannelOptions.values()) {
      if (routeChannel.routingId !== undefined) {
        return routeChannel.routingId;
      }
    }
    for (const spotNode of this.options.registration.spotNodes.values()) {
      const routingId = spotNode.routingId ?? spotNode.router?.routingId ?? spotNode.pubSub?.routingId;
      if (routingId !== undefined) {
        return routingId;
      }
    }
    return this.options.fallbackNodeRid;
  }

  async startForRuntime(
    primarySpotMeshName: string | undefined,
    spotNodeRuntime: ZLinkSpotNodeRuntimeManager,
    channelRuntime: ZLinkChannelRuntimeManager
  ): Promise<ZLinkLocationRuntime | undefined> {
    const runtime = this.ensureRuntime(primarySpotMeshName);
    if (runtime === undefined) {
      return undefined;
    }
    await runtime.start(this.ownerNodeRid(spotNodeRuntime));
    const stores = this.currentStores;
    if (stores === undefined) {
      return runtime;
    }
    spotNodeRuntime.configureLocationAutoConnect(
      runtime,
      stores,
      this.options.registration.locations.options,
      this.currentEvents
    );
    await spotNodeRuntime.startLocationAutoConnect();
    channelRuntime.configureLocationAutoConnect(
      runtime,
      stores,
      this.options.registration.locations.options,
      this.currentEvents
    );
    await channelRuntime.startLocationAutoConnect();
    return runtime;
  }

  clearForStop(): ZLinkLocationRuntimeStopSnapshot {
    const snapshot = {
      runtime: this.runtime,
      lifecycle: this.lifecycle
    };
    this.stores = undefined;
    this.runtime = undefined;
    this.lifecycle = undefined;
    this.events = undefined;
    this.leaseTrackerState = undefined;
    return snapshot;
  }

  private createRuntimeStores(): ZLinkLocationRuntimeStores | undefined {
    const locations = this.options.registration.locations;
    if (locations.useInMemoryStores) {
      const store = new ZLinkInMemoryLocationStore();
      return {
        locationStore: store,
        peerStore: store,
        spotStore: store,
        actorStore: store,
        routeStore: store,
        ownerLeaseStore: store
      };
    }
    const store = locations.storeInstance;
    if (store !== undefined) {
      return {
        locationStore: store,
        peerStore: store,
        spotStore: store,
        actorStore: store,
        routeStore: store,
        ownerLeaseStore: store
      };
    }
    return undefined;
  }

  private createAndRememberStores(): ZLinkLocationRuntimeStores | undefined {
    if (this.stores !== undefined) return this.stores;
    this.stores = this.createRuntimeStores();
    return this.stores;
  }

  private leaseTracker(stores: ZLinkLocationRuntimeStores): ZLinkOwnerLeaseTracker {
    if (this.leaseTrackerState?.stores === stores) {
      return this.leaseTrackerState.tracker;
    }
    const tracker = new ZLinkOwnerLeaseTracker({
      store: stores.ownerLeaseStore,
      options: this.options.registration.locations.options
    });
    this.leaseTrackerState = { stores, tracker };
    return tracker;
  }

  private ensureEvents(): ZLinkLocationEventSink | undefined {
    if (this.events !== undefined) {
      return this.events;
    }
    const monitoring = this.options.registration.monitoring;
    if (monitoring === undefined) {
      return undefined;
    }
    const emitter = new ZLinkLocationMonitoringEventEmitter({
      peer: monitoring.locationPeer?.[0],
      spot: monitoring.locationSpot?.[0],
      actor: monitoring.locationActor?.[0],
      route: monitoring.locationRoute?.[0]
    }, this.options.runtimeEventPublisher);
    this.events = emitter;
    return emitter;
  }
}

function isRoutingIdAllocationStore(value: unknown): value is ZLinkRoutingIdSlotAllocationStore {
  const store = value as Partial<ZLinkRoutingIdSlotAllocationStore> | undefined;
  return typeof store?.acquireRoutingIdSlot === 'function'
    && typeof store.releaseRoutingIdSlot === 'function'
    && typeof store.listRoutingIdSlots === 'function';
}
