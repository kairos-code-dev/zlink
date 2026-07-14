import type {
  RoutingId,
  ZLinkLocationOptions,
  ZLinkProviderResolver
} from '../../contracts';
import type { ZLinkSpotRouteTarget } from '../spots/spot-routing-internal';
import type { ZLinkMessageFlowModeCell } from '../diagnostics';
import type { Message } from '@zlink-systems/zlink';
import {
  ZLinkConfigurationException,
  type ZLinkFrameworkRegistration
} from '../configuration';
import type {
  ZLinkBackendContext,
  ZLinkBackendRouterSocket,
  ZLinkBackendSpot,
  ZLinkBackendSpotNode,
  ZLinkBackendSpotRouteBridge,
  ZLinkBackendSocketMonitor,
  ZLinkChannelBackendAdapter,
  ZLinkMonitoringBackendAdapter
} from '../backend/contracts';
import type { ZLinkRuntimeTaskRunner } from '../execution';
import {
  ZLinkLocationRuntime,
  type ZLinkLocationEventSink,
  type ZLinkLocationRuntimeStores
} from '../locations';
import {
  type ZLinkChannelEnvelopeCodecRegistry
} from './channel-envelope';
import { ZLinkSpotRouteBridgeRawReplyRegistry } from './spot-route-bridge-raw-reply';
import { ZLinkChannelSocketRegistry } from './channel-socket-registry';
import {
  type ZLinkRouteRuntimeRequestHandler,
  type ZLinkRouteRuntimeSendHandler
} from './channel-dispatchers';
import { ZLinkChannelDispatchServices } from './channel-dispatch-services';
import { ZLinkChannelOutboundOperations } from './channel-outbound-operations';
import { ZLinkChannelRuntimeLifecycle } from './channel-runtime-lifecycle';
import { ZLinkSpotRouteDispatchStrategy } from './spot-route-dispatch-strategy';

export class ZLinkChannelRuntimeManager {
  private readonly sockets: ZLinkChannelSocketRegistry;
  private readonly spotRoutes: ZLinkSpotRouteDispatchStrategy;
  private readonly outbound: ZLinkChannelOutboundOperations;
  private readonly lifecycle: ZLinkChannelRuntimeLifecycle;

  constructor(
    registration: ZLinkFrameworkRegistration,
    adapter: ZLinkChannelBackendAdapter,
    context: ZLinkBackendContext,
    providerResolver?: ZLinkProviderResolver,
    options: ZLinkChannelRuntimeManagerOptions = {}
  ) {
    this.sockets = new ZLinkChannelSocketRegistry(
      registration,
      adapter,
      context,
      options.monitoringAdapter,
      options.oneWayFailureSink
    );
    const codecs: ZLinkChannelEnvelopeCodecRegistry = { serializers: registration.messageSerializers };
    const dispatchServices = new ZLinkChannelDispatchServices(
      registration,
      providerResolver,
      options.messageFlowModeCell
    );
    const spotRouteBridges = new Map<string, ZLinkBackendSpotRouteBridge>();
    const spotRouteBridgeRawReplies = new ZLinkSpotRouteBridgeRawReplyRegistry();
    this.spotRoutes = new ZLinkSpotRouteDispatchStrategy({
      registration,
      sockets: this.sockets,
      codecs,
      spotRouteBridges,
      rawReplies: spotRouteBridgeRawReplies,
      localSpotRouteDispatcher: options.localSpotRouteDispatcher,
      flowCreationEnabled: () => dispatchServices.flowCreationEnabled()
    });
    this.outbound = new ZLinkChannelOutboundOperations(
      this.sockets,
      codecs,
      dispatchServices
    );
    this.lifecycle = new ZLinkChannelRuntimeLifecycle({
      registration,
      adapter,
      sockets: this.sockets,
      codecs,
      dispatchServices,
      spotRoutes: this.spotRoutes,
      spotRouteBridges,
      spotRouteBridgeRawReplies,
      internalRouteSendHandlers: options.internalRouteSendHandlers,
      internalRouteRequestHandlers: options.internalRouteRequestHandlers
    });
  }

  configureLocationAutoConnect(
    runtime: ZLinkLocationRuntime,
    stores: ZLinkLocationRuntimeStores,
    options: ZLinkLocationOptions,
    events?: ZLinkLocationEventSink
  ): void {
    this.lifecycle.configureLocationAutoConnect(runtime, stores, options, events);
  }

  async startLocationAutoConnect(signal?: AbortSignal): Promise<void> {
    await this.lifecycle.startLocationAutoConnect(signal);
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
    this.lifecycle.bindRouteMeshRouters();
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
    return this.lifecycle.start(taskRunner);
  }

  send(channelName: string, packetName: string | undefined, message: unknown, signal?: AbortSignal): void {
    this.outbound.send(channelName, packetName, message, signal);
  }

  async request<TReply>(
    channelName: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> {
    return this.outbound.request<TReply>(channelName, packetName, request, timeoutMs, signal);
  }

  publish(channelName: string, topic: string, packetName: string | undefined, event: unknown, signal?: AbortSignal): void {
    this.outbound.publish(channelName, topic, packetName, event, signal);
  }

  routeSubmit(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): void {
    this.outbound.routeSubmit(routerChannelId, targetNodeRid, packetName, message, signal);
  }

  async routeRequest<TReply>(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> {
    return this.outbound.routeRequest<TReply>(
      routerChannelId,
      targetNodeRid,
      packetName,
      request,
      timeoutMs,
      signal
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
    await this.lifecycle.dispose(signal);
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
  readonly oneWayFailureSink?: (error: unknown) => void;
}
