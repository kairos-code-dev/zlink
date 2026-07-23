import {
  ZLinkFrameworkRuntimeState,
  ZLinkLocationWriteIntent,
  ZLinkLocationWriteStatus,
  type ZLinkClientServerLocationStore,
  type ZLinkClientServerServerDescriptor,
  type ZLinkLocationOwnerToken
} from '../../contracts/Locations';
import {
  zlinkRuntimeDefaultLocationOptions,
  type ZLinkLocationOptionOverrides
} from '../../contracts/Locations/Options';
import type { ZLinkFrameworkRegistration } from '../configuration';
import { ZLinkConfigurationException } from '../configuration';
import type { ZLinkLocationRuntime, ZLinkLocationRuntimeStores } from '../locations';
import { ServiceDiscoveryRegistry } from '../foundation/service-discovery-registry';
import { ZLinkChannelSocketRegistry } from './channel-socket-registry';

interface ActiveClientServerTarget {
  readonly descriptor: ZLinkClientServerServerDescriptor;
  readonly connectionId: string;
}

/**
 * Owns the dedicated ClientServer discovery domain. It never converts these
 * descriptors into RouteMesh peer rows.
 */
export class ZLinkClientServerLocationRuntime {
  private readonly options: Required<ZLinkLocationOptionOverrides>;
  private readonly store: ZLinkClientServerLocationStore;
  private readonly discovery = new ServiceDiscoveryRegistry();
  private readonly localDescriptors = new Map<string, ZLinkClientServerServerDescriptor>();
  private readonly active = new Map<string, ActiveClientServerTarget>();
  private controller?: AbortController;
  private timer?: NodeJS.Timeout;

  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly sockets: ZLinkChannelSocketRegistry,
    private readonly locationRuntime: ZLinkLocationRuntime,
    private readonly stores: ZLinkLocationRuntimeStores,
    options: ZLinkLocationOptionOverrides
  ) {
    if (stores.clientServerStore === undefined) {
      throw new ZLinkConfigurationException(
        'ClientServer automatic discovery requires a location store with dedicated ClientServer descriptor operations.'
      );
    }
    this.store = stores.clientServerStore;
    this.options = { ...zlinkRuntimeDefaultLocationOptions, ...options };
  }

  async start(signal?: AbortSignal): Promise<void> {
    if (this.controller !== undefined) return;
    await this.publishServers(signal);
    await this.reconcileClients(signal);
    this.controller = new AbortController();
    this.schedule();
  }

  async tick(signal?: AbortSignal): Promise<void> {
    await this.publishServers(signal);
    await this.reconcileClients(signal);
  }

  async stop(signal?: AbortSignal): Promise<void> {
    this.controller?.abort();
    this.controller = undefined;
    if (this.timer !== undefined) {
      clearTimeout(this.timer);
      this.timer = undefined;
    }
    this.disconnectClients();
    await this.drainAndRemoveServers(signal);
  }

  activeTargets(channelName: string): readonly ZLinkClientServerServerDescriptor[] {
    return [...this.active.values()]
      .map((target) => target.descriptor)
      .filter((descriptor) => descriptor.channelName === channelName);
  }

  private async publishServers(signal?: AbortSignal): Promise<void> {
    const owner = this.requireOwnerToken();
    for (const [channelName, channel] of this.registration.channels) {
      if (channel.server === undefined) continue;
      const current = this.localDescriptors.get(channelName);
      if (current !== undefined) {
        const result = await this.store.updateClientServer(
          current,
          ZLinkLocationWriteIntent.Renew,
          signal
        );
        if (result.status !== ZLinkLocationWriteStatus.Stored) {
          throw new ZLinkConfigurationException(
            `ClientServer server '${channelName}' descriptor renewal was fenced.`
          );
        }
        continue;
      }
      const identity = this.sockets.clientServerServerIdentity(channelName);
      const descriptor: ZLinkClientServerServerDescriptor = {
        channelName,
        serverRid: identity.serverRid,
        lifecycleGeneration: identity.lifecycleGeneration,
        descriptorRevision: 1n,
        endpoint: identity.endpoint,
        weight: channel.server.weight ?? 100,
        state: ZLinkFrameworkRuntimeState.Serving,
        securityIdentity: 'default',
        ownerId: owner.ownerId,
        leaseGeneration: owner.leaseGeneration,
        updatedAt: new Date(0)
      };
      const result = await this.store.updateClientServer(
        descriptor,
        ZLinkLocationWriteIntent.NewClaim,
        signal
      );
      if (result.status !== ZLinkLocationWriteStatus.Stored) {
        throw new ZLinkConfigurationException(
          `ClientServer server '${channelName}' descriptor claim failed with '${result.status}'.`
        );
      }
      this.localDescriptors.set(channelName, { ...descriptor, updatedAt: result.updatedAt });
    }
  }

  private async reconcileClients(signal?: AbortSignal): Promise<void> {
    for (const [channelName, channel] of this.registration.channels) {
      if (channel.client === undefined || (channel.client.manualConnections?.length ?? 0) > 0) {
        continue;
      }
      const rows = await this.listLiveServers(channelName, signal);
      const desired = new Map(rows.map((descriptor) => [
        clientServerStableKey(descriptor),
        descriptor
      ]));
      const dealer = this.sockets.clientDealer(channelName);
      for (const [key, descriptor] of desired) {
        const current = this.active.get(key);
        const connectionId = clientServerConnectionId(descriptor);
        if (current === undefined) {
          dealer.connect(descriptor.endpoint);
          this.discovery.admitClientServer(toDiscoveryDescriptor(descriptor), connectionId);
          this.active.set(key, { descriptor, connectionId });
          continue;
        }
        if (sameDescriptor(current.descriptor, descriptor)) continue;
        if (current.descriptor.lifecycleGeneration !== descriptor.lifecycleGeneration
          || current.descriptor.endpoint !== descriptor.endpoint) {
          // One channel DEALER owns all ClientServer pipes. If a server reuses
          // its endpoint with a new lifecycle, reset that endpoint explicitly
          // so transport readiness from the old lifecycle cannot carry over.
          dealer.disconnect(current.descriptor.endpoint);
          this.discovery.removeClientServer(
            channelName,
            String(current.descriptor.serverRid),
            current.connectionId
          );
          dealer.connect(descriptor.endpoint);
          this.discovery.admitClientServer(toDiscoveryDescriptor(descriptor), connectionId);
        } else {
          this.discovery.admitClientServer(toDiscoveryDescriptor(descriptor), connectionId);
        }
        this.active.set(key, { descriptor, connectionId });
      }
      for (const [key, current] of [...this.active]) {
        if (current.descriptor.channelName !== channelName || desired.has(key)) continue;
        dealer.disconnect(current.descriptor.endpoint);
        this.discovery.removeClientServer(
          channelName,
          String(current.descriptor.serverRid),
          current.connectionId
        );
        this.active.delete(key);
      }
    }
  }

  private async listLiveServers(
    channelName: string,
    signal?: AbortSignal
  ): Promise<ZLinkClientServerServerDescriptor[]> {
    const rows: ZLinkClientServerServerDescriptor[] = [];
    let continuationToken: string | undefined;
    do {
      const page = await this.store.listClientServers(
        channelName,
        { pageSize: 1000, continuationToken },
        signal
      );
      rows.push(...page.items);
      continuationToken = page.continuationToken;
    } while (continuationToken !== undefined);

    const live: ZLinkClientServerServerDescriptor[] = [];
    for (const descriptor of rows) {
      if (descriptor.state !== ZLinkFrameworkRuntimeState.Serving || descriptor.weight === 0) {
        continue;
      }
      const lease = await this.stores.ownerLeaseStore.readOwnerLease(
        descriptor.ownerId,
        signal
      );
      if (lease.kind === 'found'
        && lease.token.leaseGeneration === descriptor.leaseGeneration
        && lease.leaseExpiresAt.getTime() > lease.storeNow.getTime()) {
        live.push(descriptor);
      }
    }
    return live;
  }

  private disconnectClients(): void {
    for (const current of this.active.values()) {
      this.sockets.clientDealer(current.descriptor.channelName)
        .disconnect(current.descriptor.endpoint);
      this.discovery.removeClientServer(
        current.descriptor.channelName,
        String(current.descriptor.serverRid),
        current.connectionId
      );
    }
    this.active.clear();
  }

  private async drainAndRemoveServers(signal?: AbortSignal): Promise<void> {
    const owner = this.locationRuntime.currentOwnerToken;
    if (owner === undefined) return;
    for (const [channelName, descriptor] of this.localDescriptors) {
      const draining = {
        ...descriptor,
        descriptorRevision: descriptor.descriptorRevision + 1n,
        state: ZLinkFrameworkRuntimeState.Draining
      };
      const result = await this.store.updateClientServer(
        draining,
        ZLinkLocationWriteIntent.Renew,
        signal
      );
      if (result.status === ZLinkLocationWriteStatus.Stored) {
        await this.store.removeClientServer({
          channelName,
          serverRid: descriptor.serverRid
        }, owner, signal);
      }
    }
    this.localDescriptors.clear();
  }

  private requireOwnerToken(): ZLinkLocationOwnerToken {
    const owner = this.locationRuntime.currentOwnerToken;
    if (owner === undefined) {
      throw new ZLinkConfigurationException(
        'ClientServer discovery requires the location owner lease to be started.'
      );
    }
    return owner;
  }

  private schedule(): void {
    const controller = this.controller;
    if (controller === undefined || controller.signal.aborted) return;
    this.timer = setTimeout(() => {
      this.timer = undefined;
      void this.tick(controller.signal)
        .catch((error) => this.locationRuntime.reportDiscoveryFailure(error))
        .finally(() => this.schedule());
    }, this.options.pollingIntervalMs);
    this.timer.unref();
  }
}

function clientServerStableKey(descriptor: ZLinkClientServerServerDescriptor): string {
  return `${descriptor.channelName}\0${String(descriptor.serverRid)}`;
}

function clientServerConnectionId(descriptor: ZLinkClientServerServerDescriptor): string {
  const channelName = descriptor.channelName;
  const serverRid = String(descriptor.serverRid);
  return `${channelName.length}:${channelName}:${serverRid.length}:${serverRid}:${descriptor.lifecycleGeneration}`;
}

function sameDescriptor(
  left: ZLinkClientServerServerDescriptor,
  right: ZLinkClientServerServerDescriptor
): boolean {
  return left.lifecycleGeneration === right.lifecycleGeneration
    && left.descriptorRevision === right.descriptorRevision
    && left.endpoint === right.endpoint
    && left.weight === right.weight
    && left.state === right.state
    && left.ownerId === right.ownerId
    && left.leaseGeneration === right.leaseGeneration;
}

function toDiscoveryDescriptor(descriptor: ZLinkClientServerServerDescriptor) {
  return {
    channelName: descriptor.channelName,
    serverRoutingId: String(descriptor.serverRid),
    lifecycleGeneration: descriptor.lifecycleGeneration,
    descriptorRevision: descriptor.descriptorRevision,
    weight: descriptor.weight,
    state: runtimeStateName(descriptor.state),
    securityIdentity: descriptor.securityIdentity,
    effectiveMaxMessageBytes: 0x7fff_ffff,
    advertisedEndpoint: descriptor.endpoint
  };
}

function runtimeStateName(
  state: ZLinkFrameworkRuntimeState
): 'preparing' | 'serving' | 'retiring' | 'stopped' | 'error' {
  switch (state) {
    case ZLinkFrameworkRuntimeState.Preparing: return 'preparing';
    case ZLinkFrameworkRuntimeState.Serving: return 'serving';
    case ZLinkFrameworkRuntimeState.Retiring:
    case ZLinkFrameworkRuntimeState.Draining: return 'retiring';
    case ZLinkFrameworkRuntimeState.Stopped: return 'stopped';
    default: return 'error';
  }
}
