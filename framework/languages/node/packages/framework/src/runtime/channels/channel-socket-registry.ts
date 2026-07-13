import {
  ZLinkSocketNativeEventType
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

export class ZLinkChannelSocketRegistry {
  private readonly clientDealers = new Map<string, ZLinkBackendDealerSocket>();
  private readonly channelRouters = new Map<string, ZLinkBackendRouterSocket>();
  private readonly publishers = new Map<string, ZLinkBackendPublisherSocket>();
  private readonly subscribers = new Map<string, ZLinkBackendSubscriberSocket>();
  private readonly routeRouters = new Map<string, ZLinkBackendRouterSocket>();
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
      ...this.channelRouters.values(),
      ...this.publishers.values(),
      ...this.subscribers.values(),
      ...this.routeRouters.values()
    ];
    this.clientDealers.clear();
    this.channelRouters.clear();
    this.publishers.clear();
    this.subscribers.clear();
    this.routeRouters.clear();
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
    if (channel.server.routingId !== undefined && channel.server.routingId.length > 0) {
      router.setRoutingId(channel.server.routingId);
    }
    if (channel.server.weight !== undefined) {
      router.peerWeight = channel.server.weight;
    }
    applySocketConfig(router, channel.server);
    this.trackSubmitter(router);
    router.bind(channel.server.bind);
    this.channelRouters.set(channelName, router);
    return router;
  }

  clientServerServerSocket(channelName: string): ZLinkBackendRouterSocket {
    return this.channelRouter(channelName);
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
      if (event.nativeEvent !== ZLinkSocketNativeEventType.Disconnected) {
        return;
      }
      this.ownedMonitors.delete(monitor);
      void monitor.dispose().catch(() => undefined);
      const submitter = this.submitters.get(router);
      submitter?.rejectActive(new ZLinkRouteDisconnectedError(
        `Route channel '${routerChannelId}' disconnected: ${event.nativeEvent}/${event.value}`
      ));
    });
  }
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
