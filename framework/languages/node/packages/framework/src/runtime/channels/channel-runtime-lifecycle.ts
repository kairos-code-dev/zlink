import { ZLinkConfigurationException } from '../configuration';
import type { ZLinkFrameworkRegistration } from '../configuration';
import type {
  ZLinkBackendRouterSocket,
  ZLinkBackendSpotRouteBridge,
  ZLinkChannelBackendAdapter
} from '../backend/contracts';
import {
  ZLINK_BACKEND_SPOT_ROUTE_BRIDGE_ROUTE_WITH_CHANNEL_INBOUND
} from '../backend/contracts';
import type { ZLinkRuntimeTaskRunner } from '../execution';
import {
  ZLinkAutoConnectLoop,
  ZLinkAutoConnectReconciler,
  ZLinkLocationRuntime,
  type ZLinkLocationEventSink,
  type ZLinkLocationRuntimeStores
} from '../locations';
import type { ZLinkLocationOptions } from '../../contracts';
import {
  buildChannelAutoConnectCapabilities,
  createChannelLocationAutoConnectContext,
  type ZLinkChannelLocationAutoConnectContext
} from './channel-autoconnect';
import {
  collectRouteChannelHandlers,
  ZLinkChannelPublishDispatcher,
  ZLinkChannelRequestDispatcher,
  ZLinkRoutePacketDispatcher,
  type ZLinkRouteHandlerRegistration,
  type ZLinkRouteRuntimeRequestHandler,
  type ZLinkRouteRuntimeSendHandler
} from './channel-dispatchers';
import {
  ZLinkChannelReceiveLoop,
  ZLinkRouteReceiveLoop,
  ZLinkSubscriberReceiveLoop
} from './channel-receive-loops';
import { ZLinkChannelDispatchServices } from './channel-dispatch-services';
import type { ZLinkChannelEnvelopeCodecRegistry } from './channel-envelope';
import { ZLinkChannelSocketRegistry } from './channel-socket-registry';
import { ZLinkSpotRouteBridgeRawReplyRegistry } from './spot-route-bridge-raw-reply';
import { ZLinkSpotRouteDispatchStrategy } from './spot-route-dispatch-strategy';

export interface ZLinkChannelRuntimeLifecycleOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly adapter: ZLinkChannelBackendAdapter;
  readonly sockets: ZLinkChannelSocketRegistry;
  readonly codecs: ZLinkChannelEnvelopeCodecRegistry;
  readonly dispatchServices: ZLinkChannelDispatchServices;
  readonly spotRoutes: ZLinkSpotRouteDispatchStrategy;
  readonly spotRouteBridges: Map<string, ZLinkBackendSpotRouteBridge>;
  readonly spotRouteBridgeRawReplies: ZLinkSpotRouteBridgeRawReplyRegistry;
  readonly internalRouteSendHandlers?: ReadonlyMap<string, ZLinkRouteRuntimeSendHandler>;
  readonly internalRouteRequestHandlers?: ReadonlyMap<string, ZLinkRouteRuntimeRequestHandler>;
}

export class ZLinkChannelRuntimeLifecycle {
  private readonly channelReceiveLoops: ZLinkChannelReceiveLoop[] = [];
  private readonly subscriberReceiveLoops: ZLinkSubscriberReceiveLoop[] = [];
  private readonly routeReceiveLoops: Array<{ stop(): Promise<void> }> = [];
  private readonly autoConnectLoops: ZLinkAutoConnectLoop[] = [];
  private locationAutoConnect?: ZLinkChannelLocationAutoConnectContext;

  constructor(private readonly options: ZLinkChannelRuntimeLifecycleOptions) {}

  configureLocationAutoConnect(
    runtime: ZLinkLocationRuntime,
    stores: ZLinkLocationRuntimeStores,
    options: ZLinkLocationOptions,
    events?: ZLinkLocationEventSink
  ): void {
    this.locationAutoConnect = createChannelLocationAutoConnectContext(runtime, stores, options, events);
  }

  async startLocationAutoConnect(signal?: AbortSignal): Promise<void> {
    const location = this.locationAutoConnect;
    if (location === undefined || this.autoConnectLoops.length > 0) {
      return;
    }

    const capabilities = buildChannelAutoConnectCapabilities(this.options.registration, this.options.sockets);
    try {
      for (const capability of capabilities) {
        const reconciler = new ZLinkAutoConnectReconciler({
          local: capability.local,
          localRow: capability.localRow,
          runtime: location.runtime,
          peerResolver: location.resolver,
          executor: capability.executor,
          reconcilePeers: capability.reconcilePeers,
          events: location.events,
          options: location.options
        });
        const loop = new ZLinkAutoConnectLoop({
          reconciler,
          local: capability.local,
          options: location.options,
          changeStampStore: location.changeStampStore,
          watchStore: location.watchStore,
          leaseTracker: location.leaseTracker
        });
        await loop.start(signal);
        this.autoConnectLoops.push(loop);
      }
    } catch (error) {
      await Promise.allSettled(this.autoConnectLoops.map((loop) => loop.stop(signal)));
      this.autoConnectLoops.length = 0;
      throw error;
    }
  }

  bindRouteMeshRouters(): void {
    for (const routeChannel of this.options.registration.routeChannelOptions.values()) {
      if (routeChannel.bind !== undefined && routeChannel.bind.trim().length > 0) {
        this.options.sockets.routeRouter(routeChannel.routerChannelId);
      }
    }
  }

  start(taskRunner?: ZLinkRuntimeTaskRunner): Promise<void>[] {
    this.openOutboundSockets();
    const tasks = [
      ...this.startChannelReceivers(taskRunner),
      ...this.startSubscriberReceivers(taskRunner),
      ...this.startRouteReceivers(taskRunner)
    ];
    return tasks;
  }

  async dispose(signal?: AbortSignal): Promise<void> {
    const channelLoops = [...this.channelReceiveLoops];
    const subscriberLoops = [...this.subscriberReceiveLoops];
    const routeLoops = [...this.routeReceiveLoops];
    const autoConnectLoops = [...this.autoConnectLoops];
    const spotRouteBridges = [...this.options.spotRouteBridges.values()];
    this.channelReceiveLoops.length = 0;
    this.subscriberReceiveLoops.length = 0;
    this.routeReceiveLoops.length = 0;
    this.autoConnectLoops.length = 0;
    this.options.spotRouteBridges.clear();
    const loopStops = [
      ...channelLoops.map((loop) => loop.stop()),
      ...subscriberLoops.map((loop) => loop.stop()),
      ...routeLoops.map((loop) => loop.stop())
    ];
    const stopped = await Promise.allSettled([
      ...loopStops,
      ...autoConnectLoops.map((loop) => loop.stop(signal))
    ]);
    this.options.spotRouteBridgeRawReplies.rejectAll(
      new ZLinkConfigurationException('Channel runtime disposed before the SPOT route reply arrived.')
    );
    const cleanup = await Promise.allSettled([
      ...spotRouteBridges.map((bridge) => bridge.dispose()),
      this.options.sockets.dispose()
    ]);
    const errors = [...stopped, ...cleanup]
      .filter((result): result is PromiseRejectedResult => result.status === 'rejected')
      .map((result) => result.reason);
    if (errors.length === 1) throw errors[0];
    if (errors.length > 1) throw new AggregateError(errors, 'Channel runtime cleanup failed.');
  }

  private openOutboundSockets(): void {
    for (const channelName of this.options.registration.channelClients) {
      this.options.sockets.clientDealer(channelName);
    }
    for (const channelName of this.options.registration.fanoutPublishers) {
      this.options.sockets.publisher(channelName);
    }
  }

  private startChannelReceivers(taskRunner: ZLinkRuntimeTaskRunner | undefined): Promise<void>[] {
    const tasks: Promise<void>[] = [];
    for (const [channelName, channel] of this.options.registration.channels) {
      if (
        channel.server?.bind === undefined ||
        ((channel.requestHandlers ?? []).length === 0 && (channel.sendHandlers ?? []).length === 0)
      ) {
        continue;
      }
      if (taskRunner === undefined) {
        throw new ZLinkConfigurationException(
          `Channel '${channelName}' handler dispatch requires a runtime task runner.`
        );
      }
      const router = this.options.sockets.channelRouter(channelName);
      const dispatcher = new ZLinkChannelRequestDispatcher({
        channelName,
        codecs: this.options.codecs,
        dispatchErrors: this.options.dispatchServices.dispatchErrorReporter(taskRunner.errorSink),
        handlers: new Map(channel.requestHandlers?.map((handler) => [handler.packetName, handler.handler])),
        sendHandlers: new Map(channel.sendHandlers?.map((handler) => [handler.packetName, handler.handler])),
        filters: this.options.dispatchServices.handlerFilters(),
        unhandled: this.options.registration.dispatch?.unhandled,
        replySubmitter: this.options.sockets.requireSubmitter(router)
      });
      const spotRouteBridge = this.createSpotRouteBridgeForRouter(channelName, router);
      const loop = new ZLinkChannelReceiveLoop(channelName, router, dispatcher, spotRouteBridge);
      this.channelReceiveLoops.push(loop);
      tasks.push(taskRunner.run(`channel:${channelName}`, (signal) => loop.run(signal)));
    }
    return tasks;
  }

  private startSubscriberReceivers(taskRunner: ZLinkRuntimeTaskRunner | undefined): Promise<void>[] {
    const tasks: Promise<void>[] = [];
    for (const [channelName, channel] of this.options.registration.channels) {
      if (channel.subscriber === undefined || (channel.publishHandlers ?? []).length === 0) {
        continue;
      }
      if (taskRunner === undefined) {
        throw new ZLinkConfigurationException(
          `Fanout channel '${channelName}' publish handler dispatch requires a runtime task runner.`
        );
      }
      const subscriber = this.options.sockets['subscriber'](channelName);
      const dispatcher = new ZLinkChannelPublishDispatcher({
        channelName,
        codecs: this.options.codecs,
        dispatchErrors: this.options.dispatchServices.dispatchErrorReporter(taskRunner.errorSink),
        handlers: new Map(channel.publishHandlers?.map((handler) => [handler.packetName, handler.handler])),
        filters: this.options.dispatchServices.handlerFilters(),
        unhandled: this.options.registration.dispatch?.unhandled,
        metrics: this.options.dispatchServices.metrics()
      });
      const loop = new ZLinkSubscriberReceiveLoop(this.options.adapter, subscriber, dispatcher);
      this.subscriberReceiveLoops.push(loop);
      tasks.push(taskRunner.run(`subscriber:${channelName}`, (signal) => loop.run(signal)));
    }
    return tasks;
  }

  private startRouteReceivers(taskRunner: ZLinkRuntimeTaskRunner | undefined): Promise<void>[] {
    const tasks: Promise<void>[] = [];
    for (const routeChannel of this.options.registration.routeChannelOptions.values()) {
      const spotRouteNode = this.options.spotRoutes.spotRouteNode(routeChannel.routerChannelId);
      if (routeChannel.bind === undefined && spotRouteNode === undefined) {
        continue;
      }
      const router = this.options.sockets.routeRouter(routeChannel.routerChannelId);
      const spotRouteBridge = this.createSpotRouteBridgeForRouter(routeChannel.routerChannelId, router);
      const handlers = [
        ...collectRouteChannelHandlers(routeChannel),
        ...[...this.options.internalRouteSendHandlers?.entries() ?? []].map(
          ([packetName, handler]): ZLinkRouteHandlerRegistration => ({ kind: 'send', packetName, handler })
        ),
        ...[...this.options.internalRouteRequestHandlers?.entries() ?? []].map(
          ([packetName, handler]): ZLinkRouteHandlerRegistration => ({ kind: 'request', packetName, handler })
        )
      ];
      if (taskRunner === undefined) {
        throw new ZLinkConfigurationException(
          `Route channel '${routeChannel.routerChannelId}' dispatch requires a runtime task runner.`
        );
      }
      const dispatcher = new ZLinkRoutePacketDispatcher({
        routerChannelId: routeChannel.routerChannelId,
        codecs: this.options.codecs,
        dispatchErrors: this.options.dispatchServices.dispatchErrorReporter(taskRunner.errorSink),
        handlers,
        filters: this.options.dispatchServices.handlerFilters(),
        unhandled: this.options.registration.dispatch?.unhandled,
        replySubmitter: this.options.sockets.requireSubmitter(router),
        spotRouteBridge,
        rawBridgeReplyHandler: (received) =>
          this.options.spotRouteBridgeRawReplies.tryComplete(routeChannel.routerChannelId, received)
      });
      const loop = new ZLinkRouteReceiveLoop(router, dispatcher);
      this.routeReceiveLoops.push(loop);
      tasks.push(taskRunner.run(`route:${routeChannel.routerChannelId}`, (signal) => loop.run(signal)));
    }
    return tasks;
  }

  private createSpotRouteBridgeForRouter(
    channelName: string,
    router: ZLinkBackendRouterSocket
  ): ZLinkBackendSpotRouteBridge | undefined {
    const spotRouteNode = this.options.spotRoutes.spotRouteNode(channelName);
    if (spotRouteNode === undefined) {
      return undefined;
    }
    const bridge = spotRouteNode.createRouteBridge();
    bridge.attachRouterChannel(channelName, router, {
      capabilities: ZLINK_BACKEND_SPOT_ROUTE_BRIDGE_ROUTE_WITH_CHANNEL_INBOUND
    });
    this.options.spotRouteBridges.set(channelName, bridge);
    return bridge;
  }
}
