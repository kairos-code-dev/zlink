import {
  ZLinkSocketNativeEventType,
  type RoutingId,
  type ZLinkClientServerServerDescriptor
} from '../../contracts';
import {
  ZLinkConfigurationException,
  type ZLinkFrameworkRegistration
} from '../configuration';
import type {
  ZLinkBackendContext,
  ZLinkBackendDealerSocket,
  ZLinkBackendPublisherSocket,
  ZLinkBackendRouterSocket,
  ZLinkBackendSocketMonitor,
  ZLinkBackendSubscriberSocket,
  ZLinkChannelBackendAdapter,
  ZLinkMonitoringBackendAdapter
} from '../backend/contracts';
import { ZLinkAsyncSubmitter } from '../messaging';
import { ZLinkRouteDisconnectedError } from './route-disconnected-error';
import { attachEndpointConnections } from '../../contracts/Configuration/RuntimeEndpointConnections';
import { ZLinkRouteMemberSnapshot } from './route-member-snapshot';
import { randomBytes, randomUUID } from 'node:crypto';
import { Message as BindingMessage, type Message } from '@zlink-systems/zlink';
import { ServiceDiscoveryRegistry } from '../foundation/service-discovery-registry';
import {
  decodeClientServerControl,
  encodeClientServerAdmit,
  encodeClientServerReject,
  isClientServerControlFrame
} from './client-server-service-wire';

const MAX_LIFECYCLE_GENERATION = 0x7fff_ffff_ffff_ffffn;

export interface ZLinkClientServerServerSocketIdentity {
  readonly serverRid: string;
  readonly lifecycleGeneration: bigint;
  readonly endpoint: string;
}

export interface ZLinkClientServerConnectionCallbacks {
  readonly onTransportReady: (routingId: string, endpoint: string) => void;
  readonly onTerminated: (routingId: string | undefined, endpoint: string) => void;
}

export class ZLinkChannelSocketRegistry {
  private readonly clientDealers = new Map<string, ZLinkBackendDealerSocket>();
  private readonly channelRouters = new Map<string, ZLinkBackendRouterSocket>();
  private readonly publishers = new Map<string, ZLinkBackendPublisherSocket>();
  private readonly subscribers = new Map<string, ZLinkBackendSubscriberSocket>();
  private readonly routeRouters = new Map<string, ZLinkBackendRouterSocket>();
  private readonly routeMembers = new ZLinkRouteMemberSnapshot();
  private readonly clientServerIdentities = new Map<string, {
    readonly serverRid: string;
    readonly lifecycleGeneration: bigint;
  }>();
  private readonly clientServerConnections = new Map<string, {
    readonly channelName: string;
    readonly endpoint: string;
    readonly dealer: ZLinkBackendDealerSocket;
    readonly monitor: ZLinkBackendSocketMonitor;
  }>();
  private readonly clientServerDiscovery = new ServiceDiscoveryRegistry();
  private readonly clientServerReadyIdentities = new Map<string, {
    readonly channelName: string;
    readonly serverRoutingId: string;
  }>();
  private readonly clientServerServerDescriptors =
    new Map<string, ZLinkClientServerServerDescriptor>();
  private readonly submitters = new WeakMap<object, ZLinkAsyncSubmitter>();
  private readonly ownedSubmitters = new Set<ZLinkAsyncSubmitter>();
  private readonly ownedMonitors = new Set<ZLinkBackendSocketMonitor>();

  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly adapter: ZLinkChannelBackendAdapter,
    private readonly context: ZLinkBackendContext,
    private readonly monitoringAdapter?: ZLinkMonitoringBackendAdapter,
    private readonly oneWayFailureSink?: (error: unknown) => void
  ) {}

  async dispose(): Promise<void> {
    const sockets = [
      ...this.clientDealers.values(),
      ...[...this.clientServerConnections.values()].map(value => value.dealer),
      ...this.channelRouters.values(),
      ...this.publishers.values(),
      ...this.subscribers.values(),
      ...this.routeRouters.values()
    ];
    this.clientDealers.clear();
    this.clientServerConnections.clear();
    this.clientServerReadyIdentities.clear();
    this.clientServerServerDescriptors.clear();
    this.channelRouters.clear();
    this.publishers.clear();
    this.subscribers.clear();
    this.routeRouters.clear();
    this.routeMembers.clear();
    this.disposeSubmitters();
    const monitors = [...this.ownedMonitors];
    this.ownedMonitors.clear();
    const cleanup = await Promise.allSettled([
      ...monitors.map((monitor) => monitor.dispose()),
      ...sockets.map((socket) => socket.dispose())
    ]);
    const errors = cleanup
      .filter((result): result is PromiseRejectedResult => result.status === 'rejected')
      .map((result) => result.reason);
    if (errors.length === 1) throw errors[0];
    if (errors.length > 1) throw new AggregateError(errors, 'Channel socket cleanup failed.');
  }

  disposeSubmitters(): void {
    for (const submitter of this.ownedSubmitters) {
      submitter.dispose();
    }
    this.ownedSubmitters.clear();
  }

  clientDealer(channelName: string): ZLinkBackendDealerSocket {
    const existing = this.clientDealers.get(channelName);
    if (existing !== undefined) {
      return existing;
    }

    const channel = this.registration.channels.get(channelName);
    const client = channel?.client;
    if (client === undefined) {
      throw new ZLinkConfigurationException(`Channel client '${channelName}' is not registered.`);
    }

    const dealer = this.adapter.createDealerSocket(this.context);
    dealer.setChannelName(channelName);
    if (channel?.routingId !== undefined && channel.routingId.length > 0) {
      dealer.setRoutingId(deriveRoutingId(channel.routingId, 'dealer'));
    }
    applySocketConfig(dealer, client);
    this.trackSubmitter(dealer);
    if ((client.manualConnections ?? []).length > 0) {
      for (const endpoint of client.manualConnections ?? []) {
        dealer.connect(endpoint);
      }
    }
    attachEndpointConnections(client, dealer);
    this.clientDealers.set(channelName, dealer);
    return dealer;
  }

  channelRouter(channelName: string): ZLinkBackendRouterSocket {
    const existing = this.channelRouters.get(channelName);
    if (existing !== undefined) {
      return existing;
    }

    const channel = this.registration.channels.get(channelName);
    if (channel?.server === undefined) {
      throw new ZLinkConfigurationException(`Channel server '${channelName}' is not registered.`);
    }
    if (channel.server.bind === undefined) {
      throw new ZLinkConfigurationException(`Channel server '${channelName}' does not define a bind endpoint.`);
    }

    const router = this.adapter.createRouterSocket(this.context);
    router.setChannelName(channelName);
    const identity = this.clientServerIdentities.get(channelName) ?? {
      serverRid: channel.server.routingId ?? `cs-${randomUUID()}`,
      lifecycleGeneration: newLifecycleGeneration()
    };
    this.clientServerIdentities.set(channelName, identity);
    router.setRoutingId(identity.serverRid);
    if (channel.server.weight !== undefined) {
      router.peerWeight = channel.server.weight;
    }
    applySocketConfig(router, channel.server);
    this.trackSubmitter(router);
    router.bind(channel.server.bind);
    this.channelRouters.set(channelName, router);
    return router;
  }

  clientServerServerIdentity(channelName: string): ZLinkClientServerServerSocketIdentity {
    const router = this.channelRouter(channelName);
    const identity = this.clientServerIdentities.get(channelName);
    const channel = this.registration.channels.get(channelName);
    if (identity === undefined || channel?.server === undefined) {
      throw new ZLinkConfigurationException(`Channel server '${channelName}' is not registered.`);
    }
    const boundEndpoint = router.lastEndpoint ?? channel.server.bind;
    if (boundEndpoint === undefined || boundEndpoint.length === 0) {
      throw new ZLinkConfigurationException(
        `Channel server '${channelName}' did not report its bound endpoint.`
      );
    }
    return {
      ...identity,
      endpoint: advertisedEndpoint(boundEndpoint, channel.server.advertiseHost)
    };
  }

  clientServerServerSocket(channelName: string): ZLinkBackendRouterSocket {
    return this.channelRouter(channelName);
  }

  openClientServerConnection(
    channelName: string,
    connectionId: string,
    endpoint: string,
    callbacks: ZLinkClientServerConnectionCallbacks
  ): ZLinkBackendDealerSocket {
    if (this.clientServerConnections.has(connectionId)) {
      throw new ZLinkConfigurationException(
        `ClientServer connection '${connectionId}' is already open.`
      );
    }
    if (this.monitoringAdapter === undefined) {
      throw new ZLinkConfigurationException(
        'Automatic ClientServer admission requires socket monitoring.'
      );
    }
    const channel = this.registration.channels.get(channelName);
    const client = channel?.client;
    if (client === undefined) {
      throw new ZLinkConfigurationException(`Channel client '${channelName}' is not registered.`);
    }
    const dealer = this.adapter.createDealerSocket(this.context);
    dealer.setChannelName(channelName);
    dealer.setRoutingId(`cs-client-${randomUUID()}`);
    applySocketConfig(dealer, client);
    this.trackSubmitter(dealer);
    const monitor = this.monitoringAdapter.openSocketMonitor(dealer);
    this.ownedMonitors.add(monitor);
    this.clientServerConnections.set(connectionId, {
      channelName,
      endpoint,
      dealer,
      monitor
    });
    try {
      monitor.onEvent((event) => {
        if (!this.clientServerConnections.has(connectionId)) return;
        const routingId = event.routingId === undefined ? undefined : String(event.routingId);
        if (event.nativeEvent === ZLinkSocketNativeEventType.ConnectionReady
          && routingId !== undefined) {
          callbacks.onTransportReady(routingId, event.remoteAddr);
          return;
        }
        if (event.nativeEvent === ZLinkSocketNativeEventType.Disconnected
          || event.nativeEvent === ZLinkSocketNativeEventType.Closed
          || event.nativeEvent === ZLinkSocketNativeEventType.HandshakeFailedNoDetail
          || event.nativeEvent === ZLinkSocketNativeEventType.HandshakeFailedProtocol
          || event.nativeEvent === ZLinkSocketNativeEventType.HandshakeFailedAuth) {
          callbacks.onTerminated(routingId, event.remoteAddr);
        }
      });
      dealer.connect(endpoint);
    } catch (error) {
      this.clientServerConnections.delete(connectionId);
      this.ownedMonitors.delete(monitor);
      const submitter = this.submitters.get(dealer);
      if (submitter !== undefined) {
        submitter.dispose();
        this.ownedSubmitters.delete(submitter);
        this.submitters.delete(dealer);
      }
      void Promise.allSettled([monitor.dispose(), dealer.dispose()]);
      throw error;
    }
    return dealer;
  }

  async closeClientServerConnection(connectionId: string): Promise<void> {
    const current = this.clientServerConnections.get(connectionId);
    if (current === undefined) return;
    this.clientServerConnections.delete(connectionId);
    const ready = this.clientServerReadyIdentities.get(connectionId);
    if (ready !== undefined) {
      this.clientServerDiscovery.removeClientServer(
        ready.channelName,
        ready.serverRoutingId,
        connectionId
      );
      this.clientServerReadyIdentities.delete(connectionId);
    }
    try {
      current.dealer.disconnect(current.endpoint);
    } catch {
      // Disposal below is the terminal cleanup if the transport already closed.
    }
    const submitter = this.submitters.get(current.dealer);
    submitter?.rejectActive(new ZLinkRouteDisconnectedError(
      `ClientServer connection '${connectionId}' closed.`
    ));
    if (submitter !== undefined) {
      submitter.dispose();
      this.ownedSubmitters.delete(submitter);
      this.submitters.delete(current.dealer);
    }
    this.ownedMonitors.delete(current.monitor);
    const results = await Promise.allSettled([
      current.monitor.dispose(),
      current.dealer.dispose()
    ]);
    const errors = results
      .filter((result): result is PromiseRejectedResult => result.status === 'rejected')
      .map(result => result.reason);
    if (errors.length === 1) throw errors[0];
    if (errors.length > 1) {
      throw new AggregateError(errors, `ClientServer connection '${connectionId}' cleanup failed.`);
    }
  }

  admitClientServerConnection(
    descriptor: Parameters<ServiceDiscoveryRegistry['admitClientServer']>[0],
    connectionId: string
  ): boolean {
    if (!this.clientServerConnections.has(connectionId)) return false;
    const admitted = this.clientServerDiscovery.admitClientServer(descriptor, connectionId);
    if (admitted) {
      this.clientServerReadyIdentities.set(connectionId, {
        channelName: descriptor.channelName,
        serverRoutingId: descriptor.serverRoutingId
      });
    }
    return admitted;
  }

  removeClientServerReady(
    channelName: string,
    serverRoutingId: string,
    connectionId: string
  ): boolean {
    const removed = this.clientServerDiscovery.removeClientServer(
      channelName,
      serverRoutingId,
      connectionId
    );
    if (removed) this.clientServerReadyIdentities.delete(connectionId);
    return removed;
  }

  selectClientServerDealer(channelName: string): ZLinkBackendDealerSocket | undefined {
    const selected = this.clientServerDiscovery.selectClientServerConnection(channelName);
    if (selected === undefined) return undefined;
    return this.clientServerConnections.get(selected.connectionId)?.dealer;
  }

  clientDealerForOutbound(channelName: string): ZLinkBackendDealerSocket | undefined {
    const channel = this.registration.channels.get(channelName);
    if (channel?.client === undefined) {
      throw new ZLinkConfigurationException(`Channel client '${channelName}' is not registered.`);
    }
    return (channel.client.manualConnections?.length ?? 0) > 0
      ? this.clientDealer(channelName)
      : this.selectClientServerDealer(channelName);
  }

  clientServerActiveTargets(
    channelName: string
  ): readonly Parameters<ServiceDiscoveryRegistry['admitClientServer']>[0][] {
    return this.clientServerDiscovery.clientServerDescriptors(channelName);
  }

  setClientServerServerDescriptor(
    descriptor: ZLinkClientServerServerDescriptor | undefined,
    channelName: string
  ): void {
    if (descriptor === undefined) {
      this.clientServerServerDescriptors.delete(channelName);
    } else {
      this.clientServerServerDescriptors.set(channelName, descriptor);
    }
  }

  tryHandleClientServerControl(
    channelName: string,
    received: {
      readonly parts: readonly Message[];
      readonly requestSeq: bigint | null;
      readonly routingId: unknown;
    },
    router: ZLinkBackendRouterSocket
  ): boolean {
    if (received.parts.length === 0) return false;
    const first = received.parts[0];
    if (!isClientServerControlFrame(first.data())) return false;
    let reply: Buffer;
    try {
      const record = decodeClientServerControl(first.data());
      if (record.kind !== 'hello'
        || received.parts.length !== 1
        || received.requestSeq === null) {
        reply = encodeClientServerReject(1);
      } else {
        const descriptor = this.clientServerServerDescriptors.get(channelName);
        if (descriptor === undefined
          || record.hello.channelName !== channelName
          || record.hello.securityIdentity !== descriptor.securityIdentity) {
          reply = encodeClientServerReject(3);
        } else {
          reply = encodeClientServerAdmit(
            descriptor,
            normalizedMessageLimit(router.maxMessageSize)
          );
        }
      }
    } catch {
      reply = encodeClientServerReject(1);
    }
    if (received.requestSeq !== null) {
      const message = BindingMessage.from(reply);
      try {
        router.reply(received.routingId as RoutingId, received.requestSeq, message);
      } finally {
        message.close();
      }
    }
    return true;
  }

  publisher(channelName: string): ZLinkBackendPublisherSocket {
    const existing = this.publishers.get(channelName);
    if (existing !== undefined) {
      return existing;
    }

    const channel = this.registration.channels.get(channelName);
    if (channel?.publisher === undefined) {
      throw new ZLinkConfigurationException(`Channel publisher '${channelName}' is not registered.`);
    }
    if (channel.publisher.bind === undefined) {
      throw new ZLinkConfigurationException(`Channel publisher '${channelName}' does not define a bind endpoint.`);
    }

    const publisher = this.adapter.createPublisherSocket(this.context);
    publisher.setChannelName(channelName);
    this.trackSubmitter(publisher);
    publisher.bind(channel.publisher.bind);
    this.publishers.set(channelName, publisher);
    return publisher;
  }

  subscriber(channelName: string): ZLinkBackendSubscriberSocket {
    const existing = this.subscribers.get(channelName);
    if (existing !== undefined) {
      return existing;
    }

    const channel = this.registration.channels.get(channelName);
    if (channel?.subscriber === undefined) {
      throw new ZLinkConfigurationException(`Channel subscriber '${channelName}' is not registered.`);
    }

    const subscriber = this.adapter.createSubscriberSocket(this.context);
    subscriber.setChannelName(channelName);
    subscriber.setSubscription('');
    if ((channel.subscriber.manualConnections ?? []).length > 0) {
      for (const endpoint of channel.subscriber.manualConnections ?? []) {
        subscriber.connect(endpoint);
      }
    }
    attachEndpointConnections(channel.subscriber, subscriber);
    this.subscribers.set(channelName, subscriber);
    return subscriber;
  }

  routeRouter(routerChannelId: string): ZLinkBackendRouterSocket {
    const existing = this.routeRouters.get(routerChannelId);
    if (existing !== undefined) {
      return existing;
    }

    const routeChannel = this.registration.routeChannelOptions.get(routerChannelId);
    if (routeChannel === undefined) {
      throw new ZLinkConfigurationException(`Route channel '${routerChannelId}' is not registered.`);
    }
    const router = this.adapter.createRouterSocket(this.context);
    router.setChannelName(routerChannelId);
    const routerOptions: unknown = 'options' in router ? router.options : undefined;
    if (
      (routeChannel.manualConnections?.length ?? 0) > 0 &&
      typeof routerOptions === 'object' &&
      routerOptions !== null &&
      'probe' in routerOptions
    ) {
      (routerOptions as { probe: boolean }).probe = true;
    }
    if (routeChannel.routingId !== undefined && routeChannel.routingId.length > 0) {
      router.setRoutingId(routeChannel.routingId);
    }
    if (routeChannel.weight !== undefined) {
      router.peerWeight = routeChannel.weight;
    }
    applySocketConfig(router, routeChannel);
    this.trackSubmitter(router);
    this.trackRouteMonitor(routerChannelId, router);
    if ((routeChannel.manualConnections ?? []).length > 0) {
      for (const endpoint of routeChannel.manualConnections ?? []) {
        router.connect(endpoint);
      }
    }
    attachEndpointConnections(routeChannel, router);
    if (routeChannel.bind !== undefined && routeChannel.bind.trim().length > 0) {
      router.bind(routeChannel.bind);
    }
    this.routeRouters.set(routerChannelId, router);
    return router;
  }

  routeMeshSocket(routerChannelId: string): ZLinkBackendRouterSocket {
    return this.routeRouter(routerChannelId);
  }

  requireSubmitter(socket: ZLinkBackendDealerSocket | ZLinkBackendPublisherSocket | ZLinkBackendRouterSocket): ZLinkAsyncSubmitter {
    const submitter = this.submitters.get(socket);
    if (submitter === undefined) {
      throw new ZLinkConfigurationException('Channel submit runtime is not started.');
    }
    return submitter;
  }

  routeMemberStatus(routerChannelId: string, targetNodeRid: string): 'unknown' | 'missing' | 'connected' | 'disconnected' {
    return this.routeMembers.status(routerChannelId, targetNodeRid);
  }

  monitorDisconnects(
    socket: ZLinkBackendDealerSocket | ZLinkBackendSubscriberSocket,
    handler: (endpoint: string) => void
  ): void {
    if (this.monitoringAdapter === undefined) {
      return;
    }
    const monitor = this.monitoringAdapter.openSocketMonitor(socket);
    this.ownedMonitors.add(monitor);
    monitor.onEvent((event) => {
      if (event.nativeEvent === ZLinkSocketNativeEventType.Disconnected && event.remoteAddr.length > 0) {
        handler(event.remoteAddr);
      }
    });
  }

  private trackSubmitter(socket: ZLinkBackendDealerSocket | ZLinkBackendPublisherSocket | ZLinkBackendRouterSocket): void {
    const submitter = new ZLinkAsyncSubmitter((handler) => socket.onSendReady(handler), {
      ...('sendTimeoutMs' in socket && socket.sendTimeoutMs !== -1
        ? { timeoutMs: socket.sendTimeoutMs }
        : {}),
      onCommandFailure: this.oneWayFailureSink
    });
    this.submitters.set(socket, submitter);
    this.ownedSubmitters.add(submitter);
  }

  private trackRouteMonitor(routerChannelId: string, router: ZLinkBackendRouterSocket): void {
    if (this.monitoringAdapter === undefined) {
      return;
    }
    const monitor = this.monitoringAdapter.openSocketMonitor(router);
    this.ownedMonitors.add(monitor);
    monitor.onEvent((event) => {
      const routingId = event.routingId === undefined ? undefined : String(event.routingId);
      if (event.nativeEvent === ZLinkSocketNativeEventType.ConnectionReady && routingId !== undefined) {
        this.routeMembers.observeReady(routerChannelId, routingId, event.remoteAddr);
        return;
      }
      if (
        event.nativeEvent !== ZLinkSocketNativeEventType.Disconnected
        && event.nativeEvent !== ZLinkSocketNativeEventType.Closed
      ) return;
      this.routeMembers.observeTermination(routerChannelId, routingId, event.remoteAddr);
      const submitter = this.submitters.get(router);
      submitter?.rejectActive(new ZLinkRouteDisconnectedError(
        `Route channel '${routerChannelId}' disconnected: ${event.nativeEvent}/${event.value}`
      ));
    });
  }
}

function deriveRoutingId(baseRoutingId: string, suffix: string): string {
  const derived = `${baseRoutingId}\0${suffix}`;
  if (Buffer.byteLength(derived, 'utf8') > 255) {
    throw new ZLinkConfigurationException(
      `Derived routing id with suffix '${suffix}' exceeds the 255 byte limit.`
    );
  }
  return derived;
}

function newLifecycleGeneration(): bigint {
  for (;;) {
    const value = randomBytes(8).readBigUInt64BE() & MAX_LIFECYCLE_GENERATION;
    if (value !== 0n) return value;
  }
}

function advertisedEndpoint(boundEndpoint: string, advertiseHost: string | undefined): string {
  if (advertiseHost === undefined) return boundEndpoint;
  const match = /^tcp:\/\/(?:\[[^\]]+\]|[^:]+):(\d+)$/.exec(boundEndpoint);
  if (match === null) {
    throw new ZLinkConfigurationException(
      `ClientServer advertised host requires a TCP endpoint, received '${boundEndpoint}'.`
    );
  }
  const host = advertiseHost.includes(':') && !advertiseHost.startsWith('[')
    ? `[${advertiseHost}]`
    : advertiseHost;
  return `tcp://${host}:${match[1]}`;
}

function normalizedMessageLimit(value: number): number {
  return Number.isSafeInteger(value) && value > 0
    ? Math.min(value, 0xffff_ffff)
    : 0x7fff_ffff;
}

function applySocketConfig(
  socket: ZLinkBackendDealerSocket | ZLinkBackendRouterSocket,
  config: {
    readonly sendHighWaterMark?: number;
    readonly receiveHighWaterMark?: number;
    readonly sendTimeoutMs?: number;
    readonly maxMessageSize?: number;
  }
): void {
  if (config.sendHighWaterMark !== undefined) {
    socket.sendHighWaterMark = config.sendHighWaterMark;
  }
  if (config.receiveHighWaterMark !== undefined) {
    socket.receiveHighWaterMark = config.receiveHighWaterMark;
  }
  if (config.sendTimeoutMs !== undefined) {
    socket.sendTimeoutMs = config.sendTimeoutMs;
  }
  if (config.maxMessageSize !== undefined) {
    socket.maxMessageSize = config.maxMessageSize;
  }
}
