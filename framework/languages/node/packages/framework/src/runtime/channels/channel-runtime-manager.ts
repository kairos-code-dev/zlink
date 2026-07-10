import type {
  ZLinkHandlerFilter,
  RoutingId,
  ZLinkProviderResolver
} from '../../contracts';
import type { ZLinkSpotRouteTarget } from '../spots/spot-routing-internal';
import type { ZLinkDiagnosticsContext, ZLinkMessageFlowModeCell } from '../diagnostics';
import {
  ZLinkMessageFlowTracer,
  createDiagnosticsContext,
  createMessageFlowModeCell,
  flowIfEnabled
} from '../diagnostics';
import {
  ZLinkDispatchErrorSurface,
  ZLinkDispatchMessageKind,
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkMessageFlowOutcome
} from '../../contracts';
import type { Message } from '@zlink-systems/zlink';
import {
  ZLinkConfigurationException,
  type ZLinkFrameworkRegistration
} from '../configuration';
import type {
  ZLinkBackendContext,
  ZLinkBackendRouterSocket,
  ZLinkBackendSendFlags,
  ZLinkBackendSpot,
  ZLinkBackendSpotNode,
  ZLinkBackendSpotRouteBridge,
  ZLinkBackendSocketMonitor,
  ZLinkChannelBackendAdapter,
  ZLinkMonitoringBackendAdapter
} from '../backend/contracts';
import {
  ZLINK_BACKEND_SPOT_ROUTE_BRIDGE_ROUTE_WITH_CHANNEL_INBOUND
} from '../backend/contracts';
import type { ZLinkRuntimeErrorSink, ZLinkRuntimeTaskRunner } from '../execution';
import {
  ZLinkAutoConnectLoop,
  ZLinkAutoConnectReconciler,
  ZLinkLocationRuntime,
  type ZLinkLocationEventSink,
  type ZLinkLocationRuntimeStores
} from '../locations';
import {
  type ZLinkLocationOptions
} from '../../contracts';
import {
  closeMessages,
  decodeChannelReply,
  encodeChannelEnvelopeParts,
  newChannelCorrelationId,
  type ZLinkChannelEnvelopeCodecRegistry,
  ZLinkChannelMessageKind
} from './channel-envelope';
import {
  ZLinkDispatchErrorReporter
} from './dispatch-error-reporter';
import { ZLinkSpotRouteBridgeRawReplyRegistry } from './spot-route-bridge-raw-reply';
import { ZLinkChannelSocketRegistry } from './channel-socket-registry';
import { throwIfAborted } from './channel-abort';
import {
  buildChannelAutoConnectCapabilities,
  createChannelLocationAutoConnectContext,
  type ZLinkChannelLocationAutoConnectContext
} from './channel-autoconnect';
import {
  ZLinkChannelReceiveLoop,
  ZLinkRouteReceiveLoop,
  ZLinkSubscriberReceiveLoop
} from './channel-receive-loops';
import { codecsForFrameworkPacket } from './channel-framework-packets';
import {
  collectRouteChannelHandlers,
  ZLinkChannelPublishDispatcher,
  ZLinkChannelRequestDispatcher,
  ZLinkRoutePacketDispatcher,
  type ZLinkRouteHandlerRegistration,
  type ZLinkRouteRuntimeRequestHandler,
  type ZLinkRouteRuntimeSendHandler
} from './channel-dispatchers';
import { ZLinkSpotRouteDispatchStrategy } from './spot-route-dispatch-strategy';

const ZLINK_SEND_DONT_WAIT = 1 as ZLinkBackendSendFlags;

export class ZLinkChannelRuntimeManager {
  private readonly channelReceiveLoops: ZLinkChannelReceiveLoop[] = [];
  private readonly subscriberReceiveLoops: ZLinkSubscriberReceiveLoop[] = [];
  private readonly routeReceiveLoops: Array<{ stop(): Promise<void> }> = [];
  private readonly spotRouteBridges = new Map<string, ZLinkBackendSpotRouteBridge>();
  private readonly spotRouteBridgeRawReplies = new ZLinkSpotRouteBridgeRawReplyRegistry();
  private readonly sockets: ZLinkChannelSocketRegistry;
  private readonly spotRoutes: ZLinkSpotRouteDispatchStrategy;
  private readonly codecs: ZLinkChannelEnvelopeCodecRegistry;
  private readonly autoConnectLoops: ZLinkAutoConnectLoop[] = [];
  private readonly dispatchErrorReporters = new WeakMap<ZLinkRuntimeErrorSink, ZLinkDispatchErrorReporter>();
  private cachedMessageFlowModeCell?: ZLinkMessageFlowModeCell;
  private cachedDiagnosticsContext?: ZLinkDiagnosticsContext;
  private locationAutoConnect?: ZLinkChannelLocationAutoConnectContext;

  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly adapter: ZLinkChannelBackendAdapter,
    context: ZLinkBackendContext,
    private readonly providerResolver?: ZLinkProviderResolver,
    private readonly options: ZLinkChannelRuntimeManagerOptions = {}
  ) {
    this.sockets = new ZLinkChannelSocketRegistry(registration, adapter, context, options.monitoringAdapter);
    this.codecs = { serializers: registration.messageSerializers };
    this.spotRoutes = new ZLinkSpotRouteDispatchStrategy({
      registration,
      sockets: this.sockets,
      codecs: this.codecs,
      spotRouteBridges: this.spotRouteBridges,
      rawReplies: this.spotRouteBridgeRawReplies,
      localSpotRouteDispatcher: options.localSpotRouteDispatcher
    });
  }

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

    const capabilities = buildChannelAutoConnectCapabilities(this.registration, this.sockets);
    try {
      for (const capability of capabilities) {
        const reconciler = new ZLinkAutoConnectReconciler({
          local: capability.local,
          localRow: capability.localRow,
          runtime: location.runtime,
          peerResolver: location.resolver,
          executor: capability.executor,
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

  setSpotNodes(spotNodes: ReadonlyMap<string, ZLinkBackendSpotNode>): void {
    this.spotRoutes.setSpotNodes(spotNodes);
  }

  clientServerServerSocket(channelName: string): ZLinkBackendRouterSocket {
    return this.sockets.channelRouter(channelName);
  }

  routeMeshSocket(routerChannelId: string): ZLinkBackendRouterSocket {
    return this.sockets.routeRouter(routerChannelId);
  }

  bindRouteMeshRouters(): void {
    for (const routeChannel of this.registration.routeChannelOptions.values()) {
      if (routeChannel.bind !== undefined && routeChannel.bind.trim().length > 0) {
        this.sockets.routeRouter(routeChannel.routerChannelId);
      }
    }
  }

  openMonitoringSource(sourceName: string, adapter: ZLinkMonitoringBackendAdapter): ZLinkBackendSocketMonitor {
    const [channelName, role] = splitMonitoringSocketSourceName(sourceName);
    switch (role) {
      case 'server':
        return adapter.openSocketMonitor(this.sockets.channelRouter(channelName));
      case 'client':
        return adapter.openSocketMonitor(this.sockets.clientDealer(channelName));
      case 'publisher':
        return adapter.openSocketMonitor(this.sockets['publisher'](channelName));
      case 'subscriber':
        return adapter.openSocketMonitor(this.sockets['subscriber'](channelName));
      case 'router':
        return adapter.openSocketMonitor(this.sockets.routeRouter(channelName));
      default:
        throw new ZLinkConfigurationException(`Monitoring socket source '${sourceName}' is not registered.`);
    }
  }

  start(taskRunner?: ZLinkRuntimeTaskRunner): Promise<void>[] {
    const tasks: Promise<void>[] = [];
    for (const channelName of this.registration.channelClients) {
      this.sockets.clientDealer(channelName);
    }
    for (const channelName of this.registration.fanoutPublishers) {
      this.sockets.publisher(channelName);
    }
    for (const [channelName, channel] of this.registration.channels) {
      if (channel.server?.bind === undefined || ((channel.requestHandlers ?? []).length === 0 && (channel.sendHandlers ?? []).length === 0)) {
        continue;
      }
      if (taskRunner === undefined) {
        throw new ZLinkConfigurationException(`Channel '${channelName}' handler dispatch requires a runtime task runner.`);
      }
      const router = this.sockets.channelRouter(channelName);
      const dispatcher = new ZLinkChannelRequestDispatcher({
        channelName,
        codecs: this.codecs,
        dispatchErrors: this.createDispatchErrorReporter(taskRunner.errorSink),
        handlers: new Map(channel.requestHandlers?.map((handler) => [handler.packetName, handler.handler])),
        sendHandlers: new Map(channel.sendHandlers?.map((handler) => [handler.packetName, handler.handler])),
        filters: this.resolveHandlerFilters(),
        replySubmitter: this.sockets.requireSubmitter(router)
      });
      const spotRouteBridge = this.createSpotRouteBridgeForRouter(channelName, router);
      const loop = new ZLinkChannelReceiveLoop(channelName, router, dispatcher, spotRouteBridge);
      this.channelReceiveLoops.push(loop);
      tasks.push(taskRunner.run(`channel:${channelName}`, (signal) => loop.run(signal)));
    }
    for (const [channelName, channel] of this.registration.channels) {
      if (channel.subscriber === undefined || (channel.publishHandlers ?? []).length === 0) {
        continue;
      }
      if (taskRunner === undefined) {
        throw new ZLinkConfigurationException(`Fanout channel '${channelName}' publish handler dispatch requires a runtime task runner.`);
      }
      const subscriber = this.sockets['subscriber'](channelName);
      const dispatcher = new ZLinkChannelPublishDispatcher({
        channelName,
        codecs: this.codecs,
        dispatchErrors: this.createDispatchErrorReporter(taskRunner.errorSink),
        handlers: new Map(channel.publishHandlers?.map((handler) => [handler.packetName, handler.handler])),
        filters: this.resolveHandlerFilters()
      });
      const loop = new ZLinkSubscriberReceiveLoop(this.adapter, subscriber, dispatcher);
      this.subscriberReceiveLoops.push(loop);
      tasks.push(taskRunner.run(`subscriber:${channelName}`, (signal) => loop.run(signal)));
    }
    for (const routeChannel of this.registration.routeChannelOptions.values()) {
      const spotRouteNode = this.spotRoutes.spotRouteNode(routeChannel.routerChannelId);
      if (routeChannel.bind !== undefined || spotRouteNode !== undefined) {
        const router = this.sockets.routeRouter(routeChannel.routerChannelId);
        const spotRouteBridge = this.createSpotRouteBridgeForRouter(routeChannel.routerChannelId, router);
        const handlers = [
          ...collectRouteChannelHandlers(routeChannel),
          ...[...this.options.internalRouteSendHandlers?.entries() ?? []].map(([packetName, handler]): ZLinkRouteHandlerRegistration => ({
            kind: 'send',
            packetName,
            handler
          })),
          ...[...this.options.internalRouteRequestHandlers?.entries() ?? []].map(([packetName, handler]): ZLinkRouteHandlerRegistration => ({
            kind: 'request',
            packetName,
            handler
          }))
        ];
        if (taskRunner === undefined) {
          throw new ZLinkConfigurationException(`Route channel '${routeChannel.routerChannelId}' dispatch requires a runtime task runner.`);
        }
        const dispatcher = new ZLinkRoutePacketDispatcher({
          routerChannelId: routeChannel.routerChannelId,
          codecs: this.codecs,
          dispatchErrors: this.createDispatchErrorReporter(taskRunner.errorSink),
          handlers,
          filters: this.resolveHandlerFilters(),
          replySubmitter: this.sockets.requireSubmitter(router),
          spotRouteBridge,
          rawBridgeReplyHandler: (received) =>
            this.spotRouteBridgeRawReplies.tryComplete(routeChannel.routerChannelId, received)
        });
        const loop = new ZLinkRouteReceiveLoop(router, dispatcher);
        this.routeReceiveLoops.push(loop);
        tasks.push(taskRunner.run(`route:${routeChannel.routerChannelId}`, (signal) => loop.run(signal)));
      }
    }
    return tasks;
  }

  private createSpotRouteBridgeForRouter(
    channelName: string,
    router: ZLinkBackendRouterSocket
  ): ZLinkBackendSpotRouteBridge | undefined {
    const spotRouteNode = this.spotRoutes.spotRouteNode(channelName);
    if (spotRouteNode === undefined) {
      return undefined;
    }
    const bridge = spotRouteNode.createRouteBridge();
    bridge.attachRouterChannel(channelName, router, {
      capabilities: ZLINK_BACKEND_SPOT_ROUTE_BRIDGE_ROUTE_WITH_CHANNEL_INBOUND
    });
    this.spotRouteBridges.set(channelName, bridge);
    return bridge;
  }

  private createDispatchErrorReporter(errorSink: ZLinkRuntimeErrorSink): ZLinkDispatchErrorReporter {
    const existing = this.dispatchErrorReporters.get(errorSink);
    if (existing !== undefined) {
      return existing;
    }
    const reporter = new ZLinkDispatchErrorReporter(
      undefined,
      undefined,
      errorSink,
      this.diagnosticsContext()
    );
    this.dispatchErrorReporters.set(errorSink, reporter);
    return reporter;
  }

  private messageFlowModeCell(): ZLinkMessageFlowModeCell {
    this.cachedMessageFlowModeCell ??= this.options.messageFlowModeCell ?? createMessageFlowModeCell(this.registration.dispatch);
    return this.cachedMessageFlowModeCell;
  }

  private diagnosticsContext(): ZLinkDiagnosticsContext {
    this.cachedDiagnosticsContext ??= createDiagnosticsContext(
      this.registration.dispatch,
      this.providerResolver,
      this.messageFlowModeCell()
    );
    return this.cachedDiagnosticsContext;
  }

  private resolvedHandlerFilters?: readonly ZLinkHandlerFilter[];

  private resolveHandlerFilters(): readonly ZLinkHandlerFilter[] {
    if (this.resolvedHandlerFilters !== undefined) {
      return this.resolvedHandlerFilters;
    }
    this.resolvedHandlerFilters = this.registration.filterTypes.map((filterType) => {
      const filter = this.providerResolver?.get?.(filterType);
      if (filter === undefined) {
        throw new ZLinkConfigurationException(`Handler filter '${filterType.name}' is not registered in the provider resolver.`);
      }
      return filter;
    });
    return this.resolvedHandlerFilters;
  }

  // Outbound (client-side) flow tracer. Built lazily and shared across all client sends —
  // reads the same live-mode cell as the inbound reporters, so the toggle covers both.
  private cachedOutboundFlow?: ZLinkMessageFlowTracer;
  private outboundFlow(): ZLinkMessageFlowTracer {
    this.cachedOutboundFlow ??= new ZLinkMessageFlowTracer(this.diagnosticsContext(), { reportRuntimeTaskException() {} });
    return this.cachedOutboundFlow;
  }

  private traceOutbound(
    outcome: ZLinkMessageFlowOutcome,
    surface: ZLinkDispatchErrorSurface,
    messageKind: ZLinkDispatchMessageKind,
    channelName: string,
    packetName: string | undefined,
    correlationId: string | undefined,
    topic?: string,
    sourceRid?: string
  ): void {
    flowIfEnabled(this.outboundFlow(), outcome)?.trace({
      outcome,
      surface,
      messageKind,
      channelName,
      packetName,
      correlationId,
      topic,
      sourceRid
    });
  }

  async send(channelName: string, packetName: string | undefined, message: unknown, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    const dealer = this.sockets.clientDealer(channelName);
    const correlationId = newChannelCorrelationId();
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Command, channelName, packetName, message, undefined, undefined, this.codecs, correlationId) as readonly Message[];
    await this.sockets.requireSubmitter(dealer).submitCommand(
      () => dealer.send(parts, ZLINK_SEND_DONT_WAIT),
      signal
    );
    this.traceOutbound(ZLinkMessageFlowOutcome.Sent, ZLinkDispatchErrorSurface.Channel, ZLinkDispatchMessageKind.Send, channelName, packetName, correlationId);
  }

  async request<TReply>(
    channelName: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> {
    throwIfAborted(signal);
    const dealer = this.sockets.clientDealer(channelName);
    const correlationId = newChannelCorrelationId();
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Request, channelName, packetName, request, timeoutMs, undefined, this.codecs, correlationId) as readonly Message[];
    this.traceOutbound(ZLinkMessageFlowOutcome.Sent, ZLinkDispatchErrorSurface.Channel, ZLinkDispatchMessageKind.Request, channelName, packetName, correlationId);
    return this.sockets.requireSubmitter(dealer).submitRequest(
      (resolve, reject) => {
        try {
          const submitted = dealer.request(
            parts,
            (result, parts) => {
              try {
                if (result !== 0) {
                  reject(new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RouteNotConnected,
                    `Channel '${channelName}' request failed with result ${result}.`,
                    true
                  ));
                  return;
                }
                const reply = decodeChannelReply<TReply>(parts as readonly Message[], this.codecs);
                this.traceOutbound(ZLinkMessageFlowOutcome.ReplyReceived, ZLinkDispatchErrorSurface.Channel, ZLinkDispatchMessageKind.Request, channelName, packetName, correlationId);
                resolve(reply);
              } catch (error) {
                reject(error);
              } finally {
                closeMessages(parts as readonly Message[]);
              }
            },
            ZLINK_SEND_DONT_WAIT,
            timeoutMs
          );
          return submitted;
        } catch (error) {
          reject(new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.RouteNotConnected,
            `Channel '${channelName}' request failed before a reply was received.`,
            true,
            error
          ));
          return true;
        }
      },
      signal,
      timeoutMs
    );
  }

  async publish(channelName: string, topic: string, packetName: string | undefined, event: unknown, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    const publisher = this.sockets['publisher'](channelName);
    const correlationId = newChannelCorrelationId();
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Publish, channelName, packetName, event, undefined, topic, this.codecs, correlationId) as readonly Message[];
    await this.sockets.requireSubmitter(publisher).submitCommand(
      () => publisher.publish(
        topic,
        parts,
        ZLINK_SEND_DONT_WAIT
      ),
      signal
    );
    this.traceOutbound(ZLinkMessageFlowOutcome.Sent, ZLinkDispatchErrorSurface.Channel, ZLinkDispatchMessageKind.Publish, channelName, packetName, correlationId, topic);
  }

  async routeSend(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    const router = this.sockets.routeRouter(routerChannelId);
    const correlationId = newChannelCorrelationId();
    const parts = encodeChannelEnvelopeParts(
      ZLinkChannelMessageKind.Command,
      routerChannelId,
      packetName,
      message,
      undefined,
      undefined,
      codecsForFrameworkPacket(packetName, this.codecs),
      correlationId
    ) as readonly Message[];
    await this.sockets.requireSubmitter(router).submitCommand(
      () => router.send(targetNodeRid, parts, ZLINK_SEND_DONT_WAIT),
      signal
    );
    this.traceOutbound(ZLinkMessageFlowOutcome.Sent, ZLinkDispatchErrorSurface.RouteMeshChannel, ZLinkDispatchMessageKind.Send, routerChannelId, packetName, correlationId, undefined, targetNodeRid);
  }

  async routeRequest<TReply>(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> {
    throwIfAborted(signal);
    const router = this.sockets.routeRouter(routerChannelId);
    const correlationId = newChannelCorrelationId();
    const parts = encodeChannelEnvelopeParts(
      ZLinkChannelMessageKind.Request,
      routerChannelId,
      packetName,
      request,
      timeoutMs,
      undefined,
      codecsForFrameworkPacket(packetName, this.codecs),
      correlationId
    ) as readonly Message[];
    this.traceOutbound(ZLinkMessageFlowOutcome.Sent, ZLinkDispatchErrorSurface.RouteMeshChannel, ZLinkDispatchMessageKind.Request, routerChannelId, packetName, correlationId, undefined, targetNodeRid);
    return this.sockets.requireSubmitter(router).submitRequest(
      (resolve, reject) => {
        let submitted: boolean;
        try {
          submitted = router.request(
            targetNodeRid,
            parts,
            (result, parts) => {
              try {
                if (result !== 0) {
                  reject(new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RouteNotConnected,
                    `Route channel '${routerChannelId}' request failed with result ${result}.`,
                    true
                  ));
                  return;
                }
                const reply = decodeChannelReply<TReply>(parts as readonly Message[], this.codecs);
                this.traceOutbound(ZLinkMessageFlowOutcome.ReplyReceived, ZLinkDispatchErrorSurface.RouteMeshChannel, ZLinkDispatchMessageKind.Request, routerChannelId, packetName, correlationId, undefined, targetNodeRid);
                resolve(reply);
              } catch (error) {
                reject(error);
              } finally {
                closeMessages(parts as readonly Message[]);
              }
            },
            ZLINK_SEND_DONT_WAIT,
            timeoutMs
          );
        } catch (error) {
          reject(new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.RouteNotConnected,
            `Route channel '${routerChannelId}' request failed before a reply was received.`,
            true,
            error
          ));
          return true;
        }
        if (!submitted) {
          try {
            closeMessages(parts);
          } finally {
            reject(new ZLinkFrameworkException(
              ZLinkFrameworkErrorKind.RouteNotConnected,
              `Route channel '${routerChannelId}' is not ready for request.`,
              true
            ));
          }
        }
        return submitted;
      },
      signal,
      timeoutMs
    );
  }

  async routeSendToSpot(
    spotRouteTarget: ZLinkSpotRouteTarget,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): Promise<void> {
    return this.spotRoutes.routeSendToSpot(spotRouteTarget, packetName, message, signal);
  }

  async routeRequestToSpot<TReply>(
    spotRouteTarget: ZLinkSpotRouteTarget,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> {
    return this.spotRoutes.routeRequestToSpot<TReply>(spotRouteTarget, packetName, request, timeoutMs, signal);
  }

  async routeRequestFromSpotToSpot<TReply>(
    sourceSpot: ZLinkBackendSpot,
    spotRouteTarget: ZLinkSpotRouteTarget,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> {
    return this.spotRoutes.routeRequestFromSpotToSpot<TReply>(sourceSpot, spotRouteTarget, packetName, request, timeoutMs, signal);
  }

  async routeSendFromSpotToSpot(
    sourceSpot: ZLinkBackendSpot,
    spotRouteTarget: ZLinkSpotRouteTarget,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): Promise<void> {
    return this.spotRoutes.routeSendFromSpotToSpot(sourceSpot, spotRouteTarget, packetName, message, signal);
  }

  async routeRequestRawFromSpotToSpot(
    sourceSpot: ZLinkBackendSpot,
    spotRouteTarget: ZLinkSpotRouteTarget,
    request: Message,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<readonly Message[]> {
    return this.spotRoutes.routeRequestRawFromSpotToSpot(sourceSpot, spotRouteTarget, request, timeoutMs, signal);
  }

  async routeRequestRawToSpot(
    spotRouteTarget: ZLinkSpotRouteTarget,
    request: Message,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<readonly Message[]> {
    return this.spotRoutes.routeRequestRawToSpot(spotRouteTarget, request, timeoutMs, signal);
  }

  canRouteChannel(routerChannelId: string): boolean {
    return this.spotRoutes.canRouteChannel(routerChannelId);
  }

  canRoutePacketChannel(routerChannelId: string): boolean {
    return this.spotRoutes.canRoutePacketChannel(routerChannelId);
  }

  async dispose(signal?: AbortSignal): Promise<void> {
    const channelLoops = [...this.channelReceiveLoops];
    const subscriberLoops = [...this.subscriberReceiveLoops];
    const routeLoops = [...this.routeReceiveLoops];
    const autoConnectLoops = [...this.autoConnectLoops];
    const spotRouteBridges = [...this.spotRouteBridges.values()];
    this.channelReceiveLoops.length = 0;
    this.subscriberReceiveLoops.length = 0;
    this.routeReceiveLoops.length = 0;
    this.autoConnectLoops.length = 0;
    this.spotRouteBridges.clear();
    const loopStops = [
      ...channelLoops.map((loop) => loop.stop()),
      ...subscriberLoops.map((loop) => loop.stop()),
      ...routeLoops.map((loop) => loop.stop())
    ];
    await Promise.allSettled(loopStops);
    await Promise.allSettled(autoConnectLoops.map((loop) => loop.stop(signal)));
    await new Promise<void>((resolve) => setImmediate(resolve));
    await Promise.all(spotRouteBridges.map((bridge) => bridge.dispose()));
    await this.sockets.dispose();
  }

}

function splitMonitoringSocketSourceName(sourceName: string): readonly [string, string] {
  const separator = sourceName.lastIndexOf('.');
  if (separator <= 0 || separator === sourceName.length - 1) {
    throw new ZLinkConfigurationException(`Monitoring socket source '${sourceName}' is not registered.`);
  }
  return [sourceName.slice(0, separator), sourceName.slice(separator + 1)];
}

export interface ZLinkChannelRuntimeManagerOptions {
  readonly internalRouteSendHandlers?: ReadonlyMap<string, ZLinkRouteRuntimeSendHandler>;
  readonly internalRouteRequestHandlers?: ReadonlyMap<string, ZLinkRouteRuntimeRequestHandler>;
  readonly monitoringAdapter?: ZLinkMonitoringBackendAdapter;
  readonly localSpotRouteDispatcher?: {
    send(
      spotRid: RoutingId,
      packetName: string | undefined,
      message: unknown,
      context: { readonly channelName: string; readonly signal?: AbortSignal }
    ): Promise<void>;
    request<TReply>(
      spotRid: RoutingId,
      packetName: string | undefined,
      request: unknown,
      context: { readonly channelName: string; readonly signal?: AbortSignal }
    ): Promise<TReply>;
  };
  readonly messageFlowModeCell?: ZLinkMessageFlowModeCell;
}
