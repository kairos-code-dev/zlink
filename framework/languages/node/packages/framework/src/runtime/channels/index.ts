import type {
  ZLinkChannelClient,
  ZLinkFanoutClient,
  ZLinkHandlerContext,
  ZLinkHandlerFilter,
  ZLinkPublishCall,
  ZLinkPublishContext,
  ZLinkRouteRequestContext,
  ZLinkRouteSendContext,
  ZLinkRequestCall,
  ZLinkSendContext,
  ZLinkRouteClient,
  ZLinkSendCall,
  ZLinkSpotRemoteAddress,
  ZLinkSpotPublisherClient,
  Type,
  RoutingId,
  ZLinkDispatchFailure,
  ZLinkProviderResolver
} from '../../contracts';
import type { ZLinkDiagnosticsContext, ZLinkMessageFlowModeCell } from '../diagnostics';
import {
  ZLinkMessageFlowTracer,
  createDiagnosticsContext,
  createMessageFlowModeCell,
  effectiveMessageFlow,
  errorLine,
  flowIfEnabled,
  writeTraceFile
} from '../diagnostics';
import {
  ZLinkAutoConnectType,
  ZLinkDispatchErrorAction,
  ZLinkDispatchErrorReason,
  ZLinkDispatchErrorSurface,
  ZLinkDispatchMessageKind,
  ZLinkMessageFlowLogMode,
  ZLinkMessageFlowOutcome,
  ZLinkMessageFlowEvent,
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';
import { invokeZLinkHandlerFilters } from '../handlers';
import type {
  DealerSocket,
  Message,
  MessageLike,
  PubSocket
} from '@zlink-systems/zlink';
import {
  ZLinkConfigurationException,
  type ZLinkFrameworkRegistration,
  type ZLinkRouteChannelOptions
} from '../configuration';
import type {
  ZLinkBackendDiscovery,
  ZLinkBackendContext,
  ZLinkBackendDealerSocket,
  ZLinkBackendPublisherSocket,
  ZLinkBackendRouterSocket,
  ZLinkBackendSpot,
  ZLinkBackendSpotNode,
  ZLinkBackendSpotRouteBridge,
  ZLinkBackendSubscriberSocket,
  ZLinkChannelBackendAdapter
} from '../backend/contracts';
import {
  ZLINK_BACKEND_SPOT_ROUTE_BRIDGE_ROUTE_WITH_CHANNEL_INBOUND
} from '../backend/contracts';
import type { ZLinkRuntimeErrorSink, ZLinkRuntimeTaskRunner } from '../execution';
import { ZLinkAsyncSubmitter } from '../messaging';

type ZLinkRuntimeDispatchFailure = ZLinkDispatchFailure & {
  readonly error?: unknown;
};

import {
  closeMessages,
  decodeChannelEnvelope,
  decodeChannelPayload,
  decodeChannelReply,
  encodeChannelEnvelopeParts,
  newChannelCorrelationId,
  encodeChannelErrorReplyParts,
  encodeChannelReplyParts,
  JSON_CONTENT_TYPE,
  type ZLinkChannelEnvelopeCodecRegistry,
  ZLinkChannelMessageKind
} from './channel-envelope';

export interface ZLinkChannelClientTransport {
  send(channelName: string, packetName: string | undefined, message: unknown, signal?: AbortSignal): Promise<void>;
  request<TReply>(
    channelName: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply>;
  publish(channelName: string, topic: string, packetName: string | undefined, event: unknown, signal?: AbortSignal): Promise<void>;
}

export interface ZLinkSpotPublisherClientTransport {
  publishSpot(channelName: string, topic: string, packetName: string | undefined, event: unknown, signal?: AbortSignal): Promise<void>;
}

export interface ZLinkRouteClientTransport {
  send(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): Promise<void>;
  request<TReply>(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply>;
}

export class ZLinkRuntimeChannelTransport implements ZLinkChannelClientTransport {
  constructor(private readonly manager: () => ZLinkChannelRuntimeManager | undefined) {}

  async send(channelName: string, packetName: string | undefined, message: unknown, signal?: AbortSignal): Promise<void> {
    return this.requireManager().send(channelName, packetName, message, signal);
  }

  async request<TReply>(
    channelName: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> {
    return this.requireManager().request(channelName, packetName, request, timeoutMs, signal);
  }

  async publish(channelName: string, topic: string, packetName: string | undefined, event: unknown, signal?: AbortSignal): Promise<void> {
    return this.requireManager().publish(channelName, topic, packetName, event, signal);
  }

  private requireManager(): ZLinkChannelRuntimeManager {
    const manager = this.manager();
    if (manager === undefined) {
      throw new ZLinkConfigurationException('Channel runtime is not started.');
    }
    return manager;
  }
}

export class ZLinkRuntimeRouteTransport implements ZLinkRouteClientTransport {
  constructor(
    private readonly manager: () => ZLinkChannelRuntimeManager | undefined,
    private readonly routeChannelPredicate: ((routerChannelId: string) => boolean) | undefined = undefined
  ) {}

  canRouteChannel(routerChannelId: string): boolean {
    const manager = this.manager();
    if (manager !== undefined) {
      return manager.canRouteChannel(routerChannelId);
    }
    return this.routeChannelPredicate?.(routerChannelId) ?? false;
  }

  canRoutePacketChannel(routerChannelId: string): boolean {
    return this.manager()?.canRoutePacketChannel(routerChannelId)
      ?? this.routeChannelPredicate?.(routerChannelId)
      ?? false;
  }

  async send(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): Promise<void> {
    return this.requireManager().routeSend(routerChannelId, targetNodeRid, packetName, message, signal);
  }

  async request<TReply>(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> {
    return this.requireManager().routeRequest(routerChannelId, targetNodeRid, packetName, request, timeoutMs, signal);
  }

  async sendToSpot(
    remoteAddress: ZLinkSpotRemoteAddress,
    message: unknown,
    options: { readonly packetName?: string; readonly signal?: AbortSignal }
  ): Promise<void> {
    return this.requireManager().routeSendToSpot(remoteAddress, options.packetName, message, options.signal);
  }

  async requestToSpot<TReply = unknown>(
    remoteAddress: ZLinkSpotRemoteAddress,
    request: unknown,
    options: { readonly packetName?: string; readonly timeoutMs?: number; readonly signal?: AbortSignal }
  ): Promise<TReply> {
    return this.requireManager().routeRequestToSpot<TReply>(
      remoteAddress,
      options.packetName,
      request,
      options.timeoutMs,
      options.signal
    );
  }

  async requestFromSpotToSpot<TReply = unknown>(
    sourceSpot: ZLinkBackendSpot,
    remoteAddress: ZLinkSpotRemoteAddress,
    request: unknown,
    options: { readonly packetName?: string; readonly timeoutMs?: number; readonly signal?: AbortSignal }
  ): Promise<TReply> {
    return this.requireManager().routeRequestFromSpotToSpot<TReply>(
      sourceSpot,
      remoteAddress,
      options.packetName,
      request,
      options.timeoutMs,
      options.signal
    );
  }

  async requestRawFromSpotToSpot(
    sourceSpot: ZLinkBackendSpot,
    remoteAddress: ZLinkSpotRemoteAddress,
    request: Message,
    options: { readonly timeoutMs?: number; readonly signal?: AbortSignal }
  ): Promise<readonly Message[]> {
    return this.requireManager().routeRequestRawFromSpotToSpot(
      sourceSpot,
      remoteAddress,
      request,
      options.timeoutMs,
      options.signal
    );
  }

  async requestRawToSpot(
    remoteAddress: ZLinkSpotRemoteAddress,
    request: Message,
    options: { readonly timeoutMs?: number; readonly signal?: AbortSignal }
  ): Promise<readonly Message[]> {
    return this.requireManager().routeRequestRawToSpot(
      remoteAddress,
      request,
      options.timeoutMs,
      options.signal
    );
  }

  private requireManager(): ZLinkChannelRuntimeManager {
    const manager = this.manager();
    if (manager === undefined) {
      throw new ZLinkConfigurationException('Route channel runtime is not started.');
    }
    return manager;
  }
}

export interface ZLinkDispatchErrorSink {
  reportRuntimeTaskException(taskName: string, error: unknown): void;
}

export class ZLinkDispatchErrorReporter {
  private reportedEvents = 0;
  private readonly ctx: ZLinkDiagnosticsContext | undefined;
  /**
   * Success-path tracer companion: every surface already receives a reporter, so
   * exposing the flow tracer here wires all dispatch sites without threading a new
   * parameter. Shares the same diagnostics context (live mode) and error sink.
   */
  readonly flow: ZLinkMessageFlowTracer;

  constructor(
    _observerType: undefined,
    providerResolver: ZLinkProviderResolver | undefined,
    private readonly errorSink: ZLinkDispatchErrorSink,
    ctx?: ZLinkDiagnosticsContext
  ) {
    this.ctx = ctx;
    const flowCtx: ZLinkDiagnosticsContext = ctx ?? {
      diagnostics: {},
      liveMode: { mode: ZLinkMessageFlowLogMode.ErrorsOnly },
      providerResolver
    };
    this.flow = new ZLinkMessageFlowTracer(flowCtx, errorSink);
  }

  report(event: ZLinkRuntimeDispatchFailure): void {
    const errorInfo = dispatchErrorInfo(event);
    this.reportedEvents += 1;
    this.flow.trace({
      outcome: ZLinkMessageFlowOutcome.Error,
      surface: event.surface,
      messageKind: event.messageKind,
      packetName: event.packetName,
      channelName: event.channelName,
      topic: event.topic,
      correlationId: event.correlationId,
      sourceRid: event.sourceRid,
      spotRid: event.spotRid,
      actorId: event.actorId,
      errorReason: event.reason,
      errorAction: event.action,
      errorType: errorInfo.errorType,
      errorMessage: errorInfo.errorMessage
    });
  }

  get reportedCount(): number {
    return this.reportedEvents;
  }

  get observerFailureCount(): number {
    return this.flow.observerFailureCount;
  }
}

function dispatchErrorInfo(event: ZLinkRuntimeDispatchFailure): { readonly errorType?: string; readonly errorMessage?: string } {
  if (event.errorType !== undefined || event.errorMessage !== undefined) {
    return { errorType: event.errorType, errorMessage: event.errorMessage };
  }
  if (event.error === undefined) {
    return {};
  }
  if (event.error instanceof Error) {
    return { errorType: event.error.name, errorMessage: event.error.message };
  }
  return { errorType: typeof event.error, errorMessage: String(event.error) };
}

function formatDispatchErrorEvent(event: ZLinkDispatchFailure): string {
  return [
    `surface=${event.surface}`,
    `messageKind=${event.messageKind}`,
    `reason=${event.reason}`,
    `action=${event.action}`,
    formatOptionalDispatchField('packetName', event.packetName),
    formatOptionalDispatchField('channelName', event.channelName),
    formatOptionalDispatchField('topic', event.topic),
    formatOptionalDispatchField('spotRid', event.spotRid),
    formatOptionalDispatchField('actorId', event.actorId),
    formatOptionalDispatchField('sourceRid', event.sourceRid),
    formatOptionalDispatchField('correlationId', event.correlationId)
  ].filter((value): value is string => value !== undefined).join(', ');
}

function formatOptionalDispatchField(name: string, value: string | undefined): string | undefined {
  return value === undefined ? undefined : `${name}=${value}`;
}

interface ZLinkPendingRawSpotRouteBridgeRequest {
  completed: boolean;
  timeout: ReturnType<typeof setTimeout> | undefined;
  abortHandler: (() => void) | undefined;
  resolve(reply: readonly Message[]): void;
  reject(error: unknown): void;
}

/**
 * Owns the per-route-channel queue of in-flight raw SPOT route-bridge requests:
 * timeout/abort wiring, dequeue-on-completion, and matching arriving raw bridge
 * replies (FIFO) to the oldest pending request. Extracted from the channel
 * runtime manager so the raw-reply state machine evolves independently of
 * channel lifecycle and route dispatch.
 */
class ZLinkSpotRouteBridgeRawReplyRegistry {
  private readonly pending = new Map<string, ZLinkPendingRawSpotRouteBridgeRequest[]>();

  enqueue(
    routerChannelId: string,
    resolve: (reply: readonly Message[]) => void,
    reject: (error: unknown) => void,
    timeoutMs: number | undefined,
    defaultTimeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): ZLinkPendingRawSpotRouteBridgeRequest {
    const pending: ZLinkPendingRawSpotRouteBridgeRequest = {
      completed: false,
      timeout: undefined,
      abortHandler: undefined,
      resolve: (reply) => {
        if (pending.completed) {
          closeMessages(reply);
          return;
        }
        pending.completed = true;
        this.remove(routerChannelId, pending);
        if (pending.timeout !== undefined) {
          clearTimeout(pending.timeout);
        }
        if (pending.abortHandler !== undefined) {
          signal?.removeEventListener('abort', pending.abortHandler);
        }
        resolve(reply);
      },
      reject: (error) => {
        if (pending.completed) {
          return;
        }
        pending.completed = true;
        this.remove(routerChannelId, pending);
        if (pending.timeout !== undefined) {
          clearTimeout(pending.timeout);
        }
        if (pending.abortHandler !== undefined) {
          signal?.removeEventListener('abort', pending.abortHandler);
        }
        reject(error);
      }
    };
    const queue = this.pending.get(routerChannelId) ?? [];
    queue.push(pending);
    this.pending.set(routerChannelId, queue);
    const effectiveTimeoutMs = timeoutMs ?? defaultTimeoutMs;
    if (effectiveTimeoutMs !== undefined) {
      pending.timeout = setTimeout(
        () => pending.reject(new ZLinkConfigurationException(`Route channel '${routerChannelId}' spot request timed out.`)),
        effectiveTimeoutMs
      );
    }
    if (signal !== undefined) {
      pending.abortHandler = () => pending.reject(new Error('The operation was aborted.'));
      signal.addEventListener('abort', pending.abortHandler, { once: true });
    }
    return pending;
  }

  remove(routerChannelId: string, pending: ZLinkPendingRawSpotRouteBridgeRequest): void {
    const queue = this.pending.get(routerChannelId);
    if (queue === undefined) {
      return;
    }
    const index = queue.indexOf(pending);
    if (index >= 0) {
      queue.splice(index, 1);
    }
    if (queue.length === 0) {
      this.pending.delete(routerChannelId);
    }
  }

  tryComplete(routerChannelId: string, received: { readonly parts: readonly Message[] }): boolean {
    const queue = this.pending.get(routerChannelId);
    if (queue === undefined || queue.length === 0 || !looksLikeRawSpotRouteBridgeReply(received.parts)) {
      return false;
    }
    queue[0].resolve(received.parts);
    return true;
  }
}

export class ZLinkChannelRuntimeManager {
  private readonly channelReceiveLoops: ZLinkChannelReceiveLoop[] = [];
  private readonly subscriberReceiveLoops: ZLinkSubscriberReceiveLoop[] = [];
  private readonly routeReceiveLoops: Array<{ stop(): void }> = [];
  private readonly spotRouteBridges = new Map<string, ZLinkBackendSpotRouteBridge>();
  private readonly spotRouteBridgeRawReplies = new ZLinkSpotRouteBridgeRawReplyRegistry();
  private readonly spotNodeRouterQueues = new Map<string, Promise<void>>();
  private readonly sockets: ZLinkChannelSocketRegistry;
  private readonly codecs: ZLinkChannelEnvelopeCodecRegistry;
  private spotNodes?: ReadonlyMap<string, ZLinkBackendSpotNode>;

  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly adapter: ZLinkChannelBackendAdapter,
    context: ZLinkBackendContext,
    private readonly providerResolver?: ZLinkProviderResolver,
    private readonly options: ZLinkChannelRuntimeManagerOptions = {}
  ) {
    this.sockets = new ZLinkChannelSocketRegistry(registration, adapter, context);
    this.codecs = { serializers: registration.messageSerializers };
  }

  setSpotNodes(spotNodes: ReadonlyMap<string, ZLinkBackendSpotNode>): void {
    this.spotNodes = spotNodes;
  }

  start(taskRunner?: ZLinkRuntimeTaskRunner): Promise<void>[] {
    const tasks: Promise<void>[] = [];
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
        filters: this.resolveHandlerFilters()
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
      const spotRouteNode = this.spotRouteNode(routeChannel.routerChannelId);
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
    const spotRouteNode = this.spotRouteNode(channelName);
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
    const cell = this.options.messageFlowModeCell ?? createMessageFlowModeCell(this.registration.dispatch);
    return new ZLinkDispatchErrorReporter(
      undefined,
      undefined,
      errorSink,
      createDiagnosticsContext(this.registration.dispatch, this.providerResolver, cell)
    );
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
    if (this.cachedOutboundFlow === undefined) {
      const cell = this.options.messageFlowModeCell ?? createMessageFlowModeCell(this.registration.dispatch);
      const ctx = createDiagnosticsContext(this.registration.dispatch, this.providerResolver, cell);
      this.cachedOutboundFlow = new ZLinkMessageFlowTracer(ctx, { reportRuntimeTaskException() {} });
    }
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
      () => dealer.send(parts, 0),
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
      (resolve, reject) => dealer.request(
        parts,
        (result, parts) => {
          try {
            if (result !== 0) {
              reject(new ZLinkConfigurationException(`Channel '${channelName}' request failed with result ${result}.`));
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
        0,
        timeoutMs
      ),
      signal
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
        0
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
      () => router.send(targetNodeRid, parts, 0),
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
        const submitted = router.request(
          targetNodeRid,
          parts,
          (result, parts) => {
            try {
              if (result !== 0) {
                reject(new ZLinkConfigurationException(`Route channel '${routerChannelId}' request failed with result ${result}.`));
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
          0,
          timeoutMs
        );
        if (!submitted) {
          try {
            closeMessages(parts);
          } finally {
            reject(new ZLinkConfigurationException(`Route channel '${routerChannelId}' is not ready for request.`));
          }
        }
        return submitted;
      },
      signal,
      timeoutMs
    );
  }

  async routeSendToSpot(
    remoteAddress: ZLinkSpotRemoteAddress,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    const parts = encodeChannelEnvelopeParts(
      ZLinkChannelMessageKind.Command,
      remoteAddress.routerChannelId,
      packetName,
      message,
      undefined,
      undefined,
      codecsForFrameworkPacket(packetName, this.codecs)
    ) as readonly Message[];
    const bridge = this.spotRouteBridges.get(remoteAddress.routerChannelId);
    if (bridge !== undefined) {
      try {
        const submitter = this.sockets.requireSubmitter(this.sockets.routeRouter(remoteAddress.routerChannelId));
        const timeoutMs = this.registration.requestTimeoutMs ?? 30_000;
        const deadline = Date.now() + timeoutMs;
        for (;;) {
          try {
            await submitter.submitCommand(
              () => {
                const submitted = appendParts(
                  bridge.send(remoteAddress.routerChannelId, remoteAddress.targetNodeRid, remoteAddress.spotRid),
                  parts
                ).submit();
                return submitted;
              },
              signal
            );
            return;
          } catch (error) {
            if (!isTransientRouteNotReadyError(error) || Date.now() >= deadline) {
              throw error;
            }
            await delay(10, signal);
          }
        }
      } finally {
        closeMessages(parts);
      }
    }
    if (this.hasBoundRouteRouter(remoteAddress.routerChannelId)) {
      const router = this.sockets.routeRouter(remoteAddress.routerChannelId);
      await this.sockets.requireSubmitter(router).submitCommand(
        () => router.sendToSpot(remoteAddress.targetNodeRid, remoteAddress.spotRid, parts, 0),
        signal
      );
      return;
    }
    const spotNodeRouter = this.spotNodeRouter(remoteAddress.routerChannelId);
    if (spotNodeRouter !== undefined) {
      try {
        await this.enqueueSpotNodeRouterOperation(remoteAddress.routerChannelId, () => {
          if (!spotNodeRouter.sendToSpot(remoteAddress.targetNodeRid, remoteAddress.spotRid, parts, 0)) {
            throw new ZLinkConfigurationException(`SpotNode router '${remoteAddress.routerChannelId}' is not ready for SPOT send.`);
          }
        });
        return;
      } finally {
        closeMessages(parts);
      }
    }
    const router = this.sockets.routeRouter(remoteAddress.routerChannelId);
    await this.sockets.requireSubmitter(router).submitCommand(
      () => router.sendToSpot(remoteAddress.targetNodeRid, remoteAddress.spotRid, parts, 0),
      signal
    );
  }

  async routeRequestToSpot<TReply>(
    remoteAddress: ZLinkSpotRemoteAddress,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> {
    throwIfAborted(signal);
    const codecs = codecsForFrameworkPacket(packetName, this.codecs);
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Request, remoteAddress.routerChannelId, packetName, request, timeoutMs, undefined, codecs) as readonly Message[];
    const bridge = this.spotRouteBridges.get(remoteAddress.routerChannelId);
    if (bridge !== undefined) {
      return this.sockets.requireSubmitter(this.sockets.routeRouter(remoteAddress.routerChannelId)).submitRequest(
        (resolve, reject) => {
          const submitted = appendParts(
            bridge.request(remoteAddress.routerChannelId, remoteAddress.targetNodeRid, remoteAddress.spotRid),
            parts
          )
            .timeout(timeoutMs ?? 0)
            .submit((result, replyParts) => {
              try {
                if (result !== 0) {
                  reject(new ZLinkConfigurationException(`Route channel '${remoteAddress.routerChannelId}' spot request failed with result ${result}.`));
                  return;
                }
                resolve(decodeChannelReply<TReply>(replyParts as readonly Message[], codecs));
              } catch (error) {
                reject(error);
              } finally {
                closeMessages(replyParts as readonly Message[]);
              }
            });
          if (!submitted) {
            try {
              closeMessages(parts);
            } finally {
              reject(new ZLinkConfigurationException(`Route channel '${remoteAddress.routerChannelId}' is not ready for SPOT request.`));
            }
          }
          return submitted;
        },
        signal,
        timeoutMs
      );
    }
    if (this.hasBoundRouteRouter(remoteAddress.routerChannelId)) {
      const router = this.sockets.routeRouter(remoteAddress.routerChannelId);
      return this.sockets.requireSubmitter(router).submitRequest(
        (resolve, reject) => {
          const submitted = router.requestToSpot(
            remoteAddress.targetNodeRid,
            remoteAddress.spotRid,
            parts,
            (result, parts) => {
              try {
                if (result !== 0) {
                  reject(new ZLinkConfigurationException(`Route channel '${remoteAddress.routerChannelId}' spot request failed with result ${result}.`));
                  return;
                }
                resolve(decodeChannelReply<TReply>(parts as readonly Message[], codecs));
              } catch (error) {
                reject(error);
              } finally {
                closeMessages(parts as readonly Message[]);
              }
            },
            0,
            timeoutMs
          );
          if (!submitted) {
            try {
              closeMessages(parts);
            } finally {
              reject(new ZLinkConfigurationException(`Route channel '${remoteAddress.routerChannelId}' is not ready for SPOT request.`));
            }
          }
          return submitted;
        },
        signal,
        timeoutMs
      );
    }
    const spotNodeRouter = this.spotNodeRouter(remoteAddress.routerChannelId);
    if (spotNodeRouter !== undefined) {
      return this.enqueueSpotNodeRouterOperation(remoteAddress.routerChannelId, () =>
        this.submitSpotNodeRouterRequest<TReply>(
          remoteAddress.routerChannelId,
          timeoutMs,
          (resolve, reject) => spotNodeRouter.requestToSpot(
            remoteAddress.targetNodeRid,
            remoteAddress.spotRid,
            parts,
            (result, replyParts) => {
              try {
                if (result !== 0) {
                  reject(new ZLinkConfigurationException(`SpotNode router '${remoteAddress.routerChannelId}' spot request failed with result ${result}.`));
                  return;
                }
                resolve(decodeChannelReply<TReply>(replyParts as readonly Message[], codecs));
              } catch (error) {
                reject(error);
              } finally {
                closeMessages(replyParts as readonly Message[]);
              }
            },
            0,
            timeoutMs
          ),
          `SpotNode router '${remoteAddress.routerChannelId}' is not ready for SPOT request.`
        ).finally(() => closeMessages(parts))
      );
    }
    const router = this.sockets.routeRouter(remoteAddress.routerChannelId);
    return this.sockets.requireSubmitter(router).submitRequest(
      (resolve, reject) => {
        const submitted = router.requestToSpot(
          remoteAddress.targetNodeRid,
          remoteAddress.spotRid,
          parts,
          (result, parts) => {
            try {
              if (result !== 0) {
                reject(new ZLinkConfigurationException(`Route channel '${remoteAddress.routerChannelId}' spot request failed with result ${result}.`));
                return;
              }
              resolve(decodeChannelReply<TReply>(parts as readonly Message[], codecs));
            } catch (error) {
              reject(error);
            } finally {
              closeMessages(parts as readonly Message[]);
            }
          },
          0,
          timeoutMs
        );
        if (!submitted) {
          try {
            closeMessages(parts);
          } finally {
            reject(new ZLinkConfigurationException(`Route channel '${remoteAddress.routerChannelId}' is not ready for SPOT request.`));
          }
        }
        return submitted;
      },
      signal,
      timeoutMs
    );
  }

  async routeRequestFromSpotToSpot<TReply>(
    sourceSpot: ZLinkBackendSpot,
    remoteAddress: ZLinkSpotRemoteAddress,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> {
    throwIfAborted(signal);
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Request, remoteAddress.routerChannelId, packetName, request, timeoutMs, undefined, this.codecs) as readonly Message[];
    return new Promise<TReply>((resolve, reject) => {
      try {
        if (!sourceSpot.requestToSpot(
          remoteAddress.targetNodeRid,
          remoteAddress.spotRid,
          parts,
          (result, replyParts) => {
            try {
              if (result !== 0) {
                reject(new ZLinkConfigurationException(`SpotNode router '${remoteAddress.routerChannelId}' spot request failed with result ${result}.`));
                return;
              }
              resolve(decodeChannelReply<TReply>(replyParts as readonly Message[], this.codecs));
            } catch (error) {
              reject(error);
            } finally {
              closeMessages(replyParts as readonly Message[]);
            }
          },
          0,
          timeoutMs
        )) {
          reject(new ZLinkConfigurationException(`SpotNode router '${remoteAddress.routerChannelId}' is not ready for SPOT request.`));
        }
      } catch (error) {
        reject(error);
      } finally {
        closeMessages(parts);
      }
    });
  }

  async routeRequestRawFromSpotToSpot(
    sourceSpot: ZLinkBackendSpot,
    remoteAddress: ZLinkSpotRemoteAddress,
    request: Message,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<readonly Message[]> {
    throwIfAborted(signal);
    return new Promise<readonly Message[]>((resolve, reject) => {
      try {
        if (!sourceSpot.requestToSpot(
          remoteAddress.targetNodeRid,
          remoteAddress.spotRid,
          request,
          (result, replyParts) => {
            if (result !== 0) {
              closeMessages(replyParts as readonly Message[]);
              reject(new ZLinkConfigurationException(`SpotNode router '${remoteAddress.routerChannelId}' spot request failed with result ${result}.`));
              return;
            }
            resolve(replyParts as readonly Message[]);
          },
          0,
          timeoutMs
        )) {
          reject(new ZLinkConfigurationException(`SpotNode router '${remoteAddress.routerChannelId}' is not ready for SPOT request.`));
        }
      } catch (error) {
        reject(error);
      }
    });
  }

  async routeRequestRawToSpot(
    remoteAddress: ZLinkSpotRemoteAddress,
    request: Message,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<readonly Message[]> {
    throwIfAborted(signal);
    const bridge = this.spotRouteBridges.get(remoteAddress.routerChannelId);
    if (bridge !== undefined) {
      return new Promise<readonly Message[]>((resolve, reject) => {
        const pending = this.spotRouteBridgeRawReplies.enqueue(
          remoteAddress.routerChannelId,
          resolve,
          reject,
          timeoutMs,
          this.registration.requestTimeoutMs,
          signal
        );
        this.sockets.requireSubmitter(this.sockets.routeRouter(remoteAddress.routerChannelId)).submitCommand(
          () => {
          const submitted = bridge.request(remoteAddress.routerChannelId, remoteAddress.targetNodeRid, remoteAddress.spotRid)
            .message(request)
            .timeout(timeoutMs ?? 0)
            .submit((result, replyParts) => {
              if (result !== 0) {
                closeMessages(replyParts as readonly Message[]);
                pending.reject(new ZLinkConfigurationException(`Route channel '${remoteAddress.routerChannelId}' spot request failed with result ${result}.`));
                return;
              }
              pending.resolve(replyParts as readonly Message[]);
            });
          if (!submitted) {
            return false;
          }
          return submitted;
          },
          signal
        ).catch((error) => pending.reject(error));
      });
    }
    const spotNodeRouter = this.spotNodeRouter(remoteAddress.routerChannelId);
    if (spotNodeRouter !== undefined) {
      return this.enqueueSpotNodeRouterOperation(remoteAddress.routerChannelId, () =>
        this.submitSpotNodeRouterRequest<readonly Message[]>(
          remoteAddress.routerChannelId,
          timeoutMs,
          (resolve, reject) => spotNodeRouter.requestToSpot(
            remoteAddress.targetNodeRid,
            remoteAddress.spotRid,
            request,
            (result, replyParts) => {
              if (result !== 0) {
                closeMessages(replyParts as readonly Message[]);
                reject(new ZLinkConfigurationException(`SpotNode router '${remoteAddress.routerChannelId}' spot request failed with result ${result}.`));
                return;
              }
              resolve(replyParts as readonly Message[]);
            },
            0,
            timeoutMs
          ),
          `SpotNode router '${remoteAddress.routerChannelId}' is not ready for SPOT request.`
      )
    );
  }

    const router = this.sockets.routeRouter(remoteAddress.routerChannelId);
    return this.sockets.requireSubmitter(router).submitRequest(
      (resolve, reject) => {
        const submitted = router.requestToSpot(
          remoteAddress.targetNodeRid,
          remoteAddress.spotRid,
          [request],
          (result, replyParts) => {
            if (result !== 0) {
              closeMessages(replyParts as readonly Message[]);
              reject(new ZLinkConfigurationException(`Route channel '${remoteAddress.routerChannelId}' spot request failed with result ${result}.`));
              return;
            }
            resolve(replyParts as readonly Message[]);
          },
          0,
          timeoutMs
        );
        if (!submitted) {
          reject(new ZLinkConfigurationException(`Route channel '${remoteAddress.routerChannelId}' is not ready for SPOT request.`));
        }
        return submitted;
      },
      signal,
      timeoutMs
    );
  }

  private spotRouteNode(routerChannelId: string): ZLinkBackendSpotNode | undefined {
    const routeChannel = this.registration.routeChannelOptions.get(routerChannelId);
    if (routeChannel === undefined) {
      return undefined;
    }
    const named = this.registration.spotNodes.get(routerChannelId);
    if (named?.router !== undefined) {
      return this.spotNodes?.get(routerChannelId);
    }
    if (routeChannel.routingId !== undefined) {
      for (const [spotNodeName, spotNode] of this.registration.spotNodes.entries()) {
        if (spotNode.router?.routingId === routeChannel.routingId) {
          return this.spotNodes?.get(spotNodeName);
        }
      }
    }
    const routerNodeNames = [...this.registration.spotNodes.entries()]
      .filter(([, spotNode]) => spotNode.router !== undefined)
      .map(([spotNodeName]) => spotNodeName);
    if (routerNodeNames.length === 1) {
      return this.spotNodes?.get(routerNodeNames[0]);
    }
    return undefined;
  }

  private spotNodeRouter(routerChannelId: string): ZLinkBackendSpot | undefined {
    if (this.registration.spotNodes.get(routerChannelId)?.router !== undefined) {
      return this.spotNodes?.get(routerChannelId)?.entrySpot();
    }
    const routeBridgeNode = this.spotRouteNode(routerChannelId);
    if (routeBridgeNode !== undefined) {
      return routeBridgeNode.entrySpot();
    }
    return this.spotNodes?.get(routerChannelId)?.entrySpot();
  }

  canRouteChannel(routerChannelId: string): boolean {
    return this.registration.routeChannels.has(routerChannelId)
      || this.spotNodeRouter(routerChannelId) !== undefined;
  }

  canRoutePacketChannel(routerChannelId: string): boolean {
    if (this.spotNodes?.has(routerChannelId) ?? false) {
      return false;
    }
    return this.registration.routeChannels.has(routerChannelId);
  }

  private hasBoundRouteRouter(routerChannelId: string): boolean {
    return this.registration.routeChannelOptions.get(routerChannelId)?.bind !== undefined;
  }

  private enqueueSpotNodeRouterOperation<T>(
    routerChannelId: string,
    operation: () => Promise<T> | T
  ): Promise<T> {
    const previous = this.spotNodeRouterQueues.get(routerChannelId) ?? Promise.resolve();
    let releaseCurrent: () => void = () => {};
    const current = new Promise<void>((resolve) => {
      releaseCurrent = resolve;
    });
    const queued = previous.catch(() => undefined).then(() => current);
    this.spotNodeRouterQueues.set(routerChannelId, queued);
    return previous
      .catch(() => undefined)
      .then(operation)
      .finally(() => {
        releaseCurrent();
        if (this.spotNodeRouterQueues.get(routerChannelId) === queued) {
          this.spotNodeRouterQueues.delete(routerChannelId);
        }
      });
  }

  private submitSpotNodeRouterRequest<T>(
    routerChannelId: string,
    timeoutMs: number | undefined,
    submit: (resolve: (reply: T) => void, reject: (error: unknown) => void) => boolean,
    notReadyMessage: string
  ): Promise<T> {
    const effectiveTimeoutMs = timeoutMs ?? this.registration.requestTimeoutMs ?? 30_000;
    const deadline = Date.now() + effectiveTimeoutMs;
    return new Promise<T>((resolve, reject) => {
      const attempt = () => {
        try {
          if (submit(resolve, reject)) {
            return;
          }
        } catch (error) {
          reject(error);
          return;
        }
        if (Date.now() >= deadline) {
          reject(new ZLinkConfigurationException(notReadyMessage));
          return;
        }
        setTimeout(attempt, 10);
      };
      attempt();
    });
  }

  async dispose(): Promise<void> {
    const channelLoops = [...this.channelReceiveLoops];
    const subscriberLoops = [...this.subscriberReceiveLoops];
    const loops = [...this.routeReceiveLoops];
    const spotRouteBridges = [...this.spotRouteBridges.values()];
    this.channelReceiveLoops.length = 0;
    this.subscriberReceiveLoops.length = 0;
    this.routeReceiveLoops.length = 0;
    this.spotRouteBridges.clear();
    for (const loop of channelLoops) {
      loop.stop();
    }
    for (const loop of subscriberLoops) {
      loop.stop();
    }
    for (const loop of loops) {
      loop.stop();
    }
    await new Promise<void>((resolve) => setImmediate(resolve));
    await Promise.all(spotRouteBridges.map((bridge) => bridge.dispose()));
    await this.sockets.dispose();
  }
}

class ZLinkChannelSocketRegistry {
  private readonly clientDealers = new Map<string, ZLinkBackendDealerSocket>();
  private readonly channelRouters = new Map<string, ZLinkBackendRouterSocket>();
  private readonly publishers = new Map<string, ZLinkBackendPublisherSocket>();
  private readonly subscribers = new Map<string, ZLinkBackendSubscriberSocket>();
  private readonly routeRouters = new Map<string, ZLinkBackendRouterSocket>();
  private readonly discoveries = new Set<ZLinkBackendDiscovery>();
  private readonly submitters = new WeakMap<object, ZLinkAsyncSubmitter>();
  private readonly ownedSubmitters = new Set<ZLinkAsyncSubmitter>();

  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly adapter: ZLinkChannelBackendAdapter,
    private readonly context: ZLinkBackendContext
  ) {}

  async dispose(): Promise<void> {
    const sockets = [
      ...this.clientDealers.values(),
      ...this.channelRouters.values(),
      ...this.publishers.values(),
      ...this.subscribers.values(),
      ...this.routeRouters.values(),
      ...this.discoveries.values()
    ];
    this.clientDealers.clear();
    this.channelRouters.clear();
    this.publishers.clear();
    this.subscribers.clear();
    this.routeRouters.clear();
    this.discoveries.clear();
    for (const submitter of this.ownedSubmitters) {
      submitter.dispose();
    }
    this.ownedSubmitters.clear();
    await Promise.all(sockets.map((socket) => socket.dispose()));
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
    this.trackSubmitter(dealer);
    if ((client.manualConnections ?? []).length > 0) {
      for (const endpoint of client.manualConnections ?? []) {
        dealer.connect(endpoint);
      }
    } else if (this.hasDiscovery()) {
      dealer.attachDiscovery(this.createDiscovery(channelName, ZLinkAutoConnectType.ClientServer));
    }
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
    this.trackSubmitter(router);
    if (channel.server.routingId !== undefined && channel.server.routingId.length > 0) {
      router.setRoutingId(channel.server.routingId);
    }
    router.bind(channel.server.bind);
    if (this.hasDiscovery()) {
      router.attachDiscovery(this.createDiscovery(channelName, ZLinkAutoConnectType.ClientServer));
    }
    this.channelRouters.set(channelName, router);
    return router;
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
    if (this.hasDiscovery()) {
      publisher.attachDiscovery(this.createDiscovery(channelName, ZLinkAutoConnectType.Fanout));
    }
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
    } else if (this.hasDiscovery()) {
      subscriber.attachDiscovery(this.createDiscovery(channelName, ZLinkAutoConnectType.Fanout));
    }
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
    this.trackSubmitter(router);
    if (
      (routeChannel.manualConnections?.length ?? 0) > 0 &&
      'options' in router &&
      typeof router.options === 'object' &&
      router.options !== null &&
      'probe' in router.options
    ) {
      (router.options as { probe: boolean }).probe = true;
    }
    if (routeChannel.routingId !== undefined && routeChannel.routingId.length > 0) {
      router.setRoutingId(routeChannel.routingId);
    }
    if (routeChannel.bind !== undefined && routeChannel.bind.trim().length > 0) {
      router.bind(routeChannel.bind);
    }
    if ((routeChannel.manualConnections ?? []).length > 0) {
      for (const endpoint of routeChannel.manualConnections ?? []) {
        router.connect(endpoint);
      }
    } else if (this.hasDiscovery()) {
      router.attachDiscovery(this.createDiscovery(routerChannelId, ZLinkAutoConnectType.RouteMesh));
    }
    this.routeRouters.set(routerChannelId, router);
    return router;
  }

  private createDiscovery(channelName: string, autoConnectType: ZLinkAutoConnectType): ZLinkBackendDiscovery {
    const discovery = this.adapter.createDiscovery(this.context, autoConnectType, channelName);
    for (const endpoint of this.registration.discovery?.registries ?? []) {
      discovery.connectRegistry(endpoint);
    }
    this.discoveries.add(discovery);
    return discovery;
  }

  private hasDiscovery(): boolean {
    return (this.registration.discovery?.registries ?? []).length > 0;
  }

  private trackSubmitter(socket: ZLinkBackendDealerSocket | ZLinkBackendPublisherSocket | ZLinkBackendRouterSocket): void {
    const submitter = new ZLinkAsyncSubmitter((handler) => socket.onSendReady(handler));
    this.submitters.set(socket, submitter);
    this.ownedSubmitters.add(submitter);
  }

  requireSubmitter(socket: ZLinkBackendDealerSocket | ZLinkBackendPublisherSocket | ZLinkBackendRouterSocket): ZLinkAsyncSubmitter {
    const submitter = this.submitters.get(socket);
    if (submitter === undefined) {
      throw new ZLinkConfigurationException('Channel submit runtime is not started.');
    }
    return submitter;
  }
}

export class ZLinkDealerChannelClientTransport implements ZLinkChannelClientTransport {
  constructor(
    private readonly dealer: DealerSocket,
    private readonly publisher?: PubSocket
  ) {}

  async send(channelName: string, packetName: string | undefined, message: Message, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    appendParts(
      this.dealer.send(),
      encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Command, channelName, packetName, message)
    ).submit();
  }

  async request<TReply>(
    channelName: string,
    packetName: string | undefined,
    request: Message,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> {
    throwIfAborted(signal);
    const operation = appendParts(
      this.dealer.request(),
      encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Request, channelName, packetName, request, timeoutMs)
    );
    if (timeoutMs !== undefined) {
      operation.timeout(timeoutMs);
    }
    const reply = await submitRequestOperation(operation);
    return decodeChannelReply<TReply>(reply);
  }

  async publish(channelName: string, topic: string, packetName: string | undefined, event: Message, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    if (this.publisher === undefined) {
      throw new ZLinkConfigurationException('Channel publisher runtime is not started.');
    }
    appendParts(
      this.publisher.publish(topic),
      encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Publish, channelName, packetName, event, undefined, topic)
    ).submit();
  }
}

export interface ZLinkChannelRequestDispatcherOptions {
  readonly channelName: string;
  readonly codecs?: ZLinkChannelEnvelopeCodecRegistry;
  readonly dispatchErrors: ZLinkDispatchErrorReporter;
  readonly handlers: ReadonlyMap<string, ZLinkChannelRequestHandler>;
  readonly sendHandlers?: ReadonlyMap<string, ZLinkChannelSendHandler>;
  readonly filters?: readonly ZLinkHandlerFilter[];
}

export interface ZLinkChannelRequestHandler {
  handle(payload: unknown, context: ZLinkHandlerContext): Promise<unknown> | unknown;
}

export interface ZLinkChannelSendHandler {
  handle(payload: unknown, context: ZLinkSendContext): Promise<void> | void;
}

export interface ZLinkRouteHandlerRegistration {
  readonly kind: 'send' | 'request';
  readonly packetName: string;
  readonly handler: ZLinkRouteRuntimeSendHandler | ZLinkRouteRuntimeRequestHandler;
}

export interface ZLinkRouteRuntimeSendHandler {
  handle(payload: unknown, context: ZLinkRouteSendContext): Promise<void> | void;
}

export interface ZLinkRouteRuntimeRequestHandler {
  handle(payload: unknown, context: ZLinkRouteRequestContext): Promise<unknown> | unknown;
}

export interface ZLinkChannelRuntimeManagerOptions {
  readonly internalRouteSendHandlers?: ReadonlyMap<string, ZLinkRouteRuntimeSendHandler>;
  readonly internalRouteRequestHandlers?: ReadonlyMap<string, ZLinkRouteRuntimeRequestHandler>;
  readonly messageFlowModeCell?: ZLinkMessageFlowModeCell;
}

export class ZLinkChannelRequestDispatcher {
  private readonly filters: readonly ZLinkHandlerFilter[];

  constructor(private readonly options: ZLinkChannelRequestDispatcherOptions) {
    this.filters = options.filters ?? [];
  }

  private traceChannelFlow(
    outcome: ZLinkMessageFlowOutcome,
    messageKind: ZLinkDispatchMessageKind,
    packetName: string,
    correlationId: string | undefined
  ): void {
    const flow = this.options.dispatchErrors.flow;
    if (flow.enabled(outcome)) {
      flow.trace({
        outcome,
        surface: ZLinkDispatchErrorSurface.Channel,
        messageKind,
        packetName,
        channelName: this.options.channelName,
        correlationId
      });
    }
  }

  async dispatch(received: {
    parts: readonly Message[];
    routingId: unknown;
    spotRid?: unknown;
    requestSeq: bigint | null;
    send?: () => ZLinkMultipartOperation<ZLinkMultipartSubmitOperation>;
  }, router: {
    reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
  }): Promise<boolean | void> {
    if (received.spotRid !== null && received.spotRid !== undefined) {
      if (received.send === undefined) {
        throw new ZLinkConfigurationException('Routed SPOT packet is missing a local SPOT delivery context.');
      }
      appendParts(received.send(), received.parts).submit();
      return true;
    }
    if (received.parts.length === 0 || received.parts[0].data().length === 0) {
      return;
    }
    const envelope = decodeChannelEnvelope(received.parts);
    const packetName = envelope.packetName;
    if (packetName === undefined) {
      throw new ZLinkConfigurationException('Channel packet is missing packetName.');
    }
    const correlationId = envelope.header.correlationId ?? undefined;
    if (envelope.header.kind === ZLinkChannelMessageKind.Command) {
      this.traceChannelFlow(ZLinkMessageFlowOutcome.Received, ZLinkDispatchMessageKind.Send, packetName, correlationId);
      const handler = this.options.sendHandlers?.get(packetName);
      if (handler === undefined) {
        this.options.dispatchErrors.report({
          surface: ZLinkDispatchErrorSurface.Channel,
          messageKind: ZLinkDispatchMessageKind.Send,
          reason: ZLinkDispatchErrorReason.HandlerMissing,
          action: ZLinkDispatchErrorAction.Drop,
          packetName,
          channelName: this.options.channelName,
          correlationId
        });
        return;
      }
      const context: ZLinkSendContext = {
        channelName: this.options.channelName,
        contentType: envelope.header.contentType,
        packetName
      };
      try {
        const payload = decodeChannelPayload(envelope, this.options.codecs);
        await invokeZLinkHandlerFilters(
          this.filters,
          { message: payload, context, channelName: this.options.channelName, packetName },
          () => Promise.resolve(handler.handle(payload, context))
        );
        this.traceChannelFlow(ZLinkMessageFlowOutcome.Dispatched, ZLinkDispatchMessageKind.Send, packetName, correlationId);
      } catch (error) {
        this.options.dispatchErrors.report({
          surface: ZLinkDispatchErrorSurface.Channel,
          messageKind: ZLinkDispatchMessageKind.Send,
          reason: ZLinkDispatchErrorReason.HandlerException,
          action: ZLinkDispatchErrorAction.Drop,
          packetName,
          channelName: this.options.channelName,
          correlationId: envelope.header.correlationId ?? undefined,
          error
        });
      }
      return;
    }
    if (envelope.header.kind !== ZLinkChannelMessageKind.Request) {
      return;
    }
    const handler = this.options.handlers.get(packetName);
    if (handler === undefined) {
      if (received.requestSeq === null) {
        this.options.dispatchErrors.report({
          surface: ZLinkDispatchErrorSurface.Channel,
          messageKind: ZLinkDispatchMessageKind.Request,
          reason: ZLinkDispatchErrorReason.ReplyPathMissing,
          action: ZLinkDispatchErrorAction.Drop,
          packetName,
          channelName: this.options.channelName,
          correlationId: envelope.header.correlationId ?? undefined
        });
        return;
      }
      appendParts(
        router.reply(received.routingId, received.requestSeq),
        encodeChannelErrorReplyParts(envelope.header, `No channel request handler is registered for '${this.options.channelName}:${packetName}'.`)
      ).submit();
      this.options.dispatchErrors.report({
        surface: ZLinkDispatchErrorSurface.Channel,
        messageKind: ZLinkDispatchMessageKind.Request,
        reason: ZLinkDispatchErrorReason.HandlerMissing,
        action: ZLinkDispatchErrorAction.ReplyError,
        packetName,
        channelName: this.options.channelName,
        correlationId: envelope.header.correlationId ?? undefined
      });
      return;
    }
    if (received.requestSeq === null) {
      throw new ZLinkConfigurationException('Channel request cannot be replied to because requestSeq is missing.');
    }

    this.traceChannelFlow(ZLinkMessageFlowOutcome.Received, ZLinkDispatchMessageKind.Request, packetName, correlationId);
    const context: ZLinkHandlerContext = {
      channelName: this.options.channelName,
      contentType: envelope.header.contentType,
      packetName
    };
    try {
      const payload = decodeChannelPayload(envelope, this.options.codecs);
      const reply = await invokeZLinkHandlerFilters(
        this.filters,
        { message: payload, context, channelName: this.options.channelName, packetName },
        () => Promise.resolve(handler.handle(payload, context))
      );
      try {
        appendParts(
          router.reply(received.routingId, received.requestSeq),
          encodeChannelReplyParts(envelope.header, reply, this.options.codecs)
        ).submit();
        this.traceChannelFlow(ZLinkMessageFlowOutcome.Replied, ZLinkDispatchMessageKind.Request, packetName, correlationId);
      } catch (error) {
        this.options.dispatchErrors.report({
          surface: ZLinkDispatchErrorSurface.Channel,
          messageKind: ZLinkDispatchMessageKind.Request,
          reason: ZLinkDispatchErrorReason.UnexpectedReply,
          action: ZLinkDispatchErrorAction.Drop,
          packetName,
          channelName: this.options.channelName,
          correlationId: envelope.header.correlationId ?? undefined,
          error
        });
      }
    } catch (error) {
      try {
        appendParts(
          router.reply(received.routingId, received.requestSeq),
          encodeChannelErrorReplyParts(envelope.header, error instanceof Error ? error.message : String(error))
        ).submit();
      } catch (replyError) {
        this.options.dispatchErrors.report({
          surface: ZLinkDispatchErrorSurface.Channel,
          messageKind: ZLinkDispatchMessageKind.Request,
          reason: ZLinkDispatchErrorReason.UnexpectedReply,
          action: ZLinkDispatchErrorAction.Drop,
          packetName,
          channelName: this.options.channelName,
          correlationId: envelope.header.correlationId ?? undefined,
          error: replyError
        });
        return;
      }
      this.options.dispatchErrors.report({
        surface: ZLinkDispatchErrorSurface.Channel,
        messageKind: ZLinkDispatchMessageKind.Request,
        reason: error instanceof ZLinkFrameworkException && error.kind === ZLinkFrameworkErrorKind.PayloadDecodeFailed
          ? ZLinkDispatchErrorReason.PayloadDecodeFailed
          : ZLinkDispatchErrorReason.HandlerException,
        action: ZLinkDispatchErrorAction.ReplyError,
        packetName,
        channelName: this.options.channelName,
        correlationId: envelope.header.correlationId ?? undefined,
        error
      });
    }
  }
}

export class ZLinkChannelReceiveLoop {
  private stopped = false;

  constructor(
    private readonly channelName: string,
    private readonly router: ZLinkBackendRouterSocket & {
      reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
    },
    private readonly dispatcher: ZLinkChannelRequestDispatcher,
    private readonly spotRouteBridge?: ZLinkBackendSpotRouteBridge
  ) {}

  async run(signal?: AbortSignal): Promise<void> {
    while (!this.stopped && signal?.aborted !== true) {
      const received = this.router.recv(1);
      if (received === undefined) {
        await new Promise<void>((resolve) => setImmediate(resolve));
        continue;
      }
      void this.dispatchAndClose(received);
    }
  }

  stop(): void {
    this.stopped = true;
  }

  private async dispatchAndClose(received: {
    parts: readonly Message[];
    routingId: unknown;
    spotRid?: unknown;
    requestSeq: bigint | null;
    send?: () => ZLinkMultipartOperation<ZLinkMultipartSubmitOperation>;
    close(): void;
  }): Promise<void> {
    let closeReceived = true;
    try {
      if (this.spotRouteBridge?.handleRouterReceived(
        this.channelName,
        received.routingId as RoutingId,
        received.requestSeq ?? 0n,
        received.parts
      ) === true) {
        closeReceived = false;
        return;
      }
      const consumed = await this.dispatcher.dispatch(received, this.router);
      if (consumed === true) {
        closeReceived = false;
      }
    } finally {
      if (closeReceived) {
        received.close();
      }
    }
  }
}

export interface ZLinkChannelPublishDispatcherOptions {
  readonly channelName: string;
  readonly codecs?: ZLinkChannelEnvelopeCodecRegistry;
  readonly dispatchErrors: ZLinkDispatchErrorReporter;
  readonly handlers: ReadonlyMap<string, ZLinkRuntimePublishHandler>;
  readonly filters?: readonly ZLinkHandlerFilter[];
}

export interface ZLinkRuntimePublishHandler {
  handle(payload: unknown, context: ZLinkPublishContext): Promise<void> | void;
}

export class ZLinkChannelPublishDispatcher {
  private readonly filters: readonly ZLinkHandlerFilter[];

  constructor(private readonly options: ZLinkChannelPublishDispatcherOptions) {
    this.filters = options.filters ?? [];
  }

  async dispatch(topicMessage: { readonly topic: string; readonly parts: readonly Message[] }): Promise<void> {
    if (topicMessage.parts.length === 0 || topicMessage.parts[0].data().length === 0) {
      return;
    }
    const envelope = decodeChannelEnvelope(topicMessage.parts);
    if (envelope.header.kind !== ZLinkChannelMessageKind.Publish) {
      return;
    }
    const packetName = envelope.packetName;
    if (packetName === undefined) {
      throw new ZLinkConfigurationException('Fanout publish message is missing packetName.');
    }
    const publishTopic = envelope.header.topic ?? topicMessage.topic;
    const publishSource = envelope.header.source ?? undefined;
    const publishCorr = envelope.header.correlationId ?? undefined;
    const flow = this.options.dispatchErrors.flow;
    if (flow.enabled(ZLinkMessageFlowOutcome.Received)) {
      flow.trace({
        outcome: ZLinkMessageFlowOutcome.Received,
        surface: ZLinkDispatchErrorSurface.Channel,
        messageKind: ZLinkDispatchMessageKind.Publish,
        packetName,
        channelName: this.options.channelName,
        topic: publishTopic,
        sourceRid: publishSource,
        correlationId: publishCorr
      });
    }
    const handler = this.options.handlers.get(packetName);
    if (handler === undefined) {
      this.options.dispatchErrors.report({
        surface: ZLinkDispatchErrorSurface.Channel,
        messageKind: ZLinkDispatchMessageKind.Publish,
        reason: ZLinkDispatchErrorReason.HandlerMissing,
        action: ZLinkDispatchErrorAction.Drop,
        packetName,
        channelName: this.options.channelName,
        topic: publishTopic,
        sourceRid: publishSource,
        correlationId: publishCorr
      });
      return;
    }

    const context: ZLinkPublishContext = {
      channelName: this.options.channelName,
      packetName,
      contentType: envelope.header.contentType,
      topic: publishTopic,
      source: publishSource
    };
    try {
      const payload = decodeChannelPayload(envelope, this.options.codecs);
      await invokeZLinkHandlerFilters(
        this.filters,
        { message: payload, context, channelName: this.options.channelName, packetName },
        () => Promise.resolve(handler.handle(payload, context))
      );
      if (flow.enabled(ZLinkMessageFlowOutcome.Dispatched)) {
        flow.trace({
          outcome: ZLinkMessageFlowOutcome.Dispatched,
          surface: ZLinkDispatchErrorSurface.Channel,
          messageKind: ZLinkDispatchMessageKind.Publish,
          packetName,
          channelName: this.options.channelName,
          topic: publishTopic,
          sourceRid: publishSource,
          correlationId: publishCorr
        });
      }
    } catch (error) {
      this.options.dispatchErrors.report({
        surface: ZLinkDispatchErrorSurface.Channel,
        messageKind: ZLinkDispatchMessageKind.Publish,
        reason: ZLinkDispatchErrorReason.HandlerException,
        action: ZLinkDispatchErrorAction.Drop,
        packetName,
        channelName: this.options.channelName,
        topic: context.topic,
        sourceRid: context.source,
        correlationId: envelope.header.correlationId ?? undefined,
        error
      });
    }
  }
}

export class ZLinkSubscriberReceiveLoop {
  private stopped = false;

  constructor(
    private readonly adapter: ZLinkChannelBackendAdapter,
    private readonly subscriber: ZLinkBackendSubscriberSocket,
    private readonly dispatcher: ZLinkChannelPublishDispatcher
  ) {
    this.poller = adapter.createReadablePoller(subscriber);
  }

  private readonly poller: ReturnType<ZLinkChannelBackendAdapter['createReadablePoller']>;

  async run(signal?: AbortSignal): Promise<void> {
    while (!this.stopped && signal?.aborted !== true) {
      if (!this.poller.wait(10)) {
        await new Promise<void>((resolve) => setImmediate(resolve));
        continue;
      }
      const topicMessage = this.adapter.createTopicMessage();
      this.subscriber.subscribe(topicMessage);
      try {
        await this.dispatcher.dispatch(topicMessage);
      } finally {
        closeMessages(topicMessage.parts as readonly Message[]);
      }
    }
  }

  stop(): void {
    this.stopped = true;
    this.poller.dispose();
  }
}

export interface ZLinkRoutePacketDispatcherOptions {
  readonly routerChannelId: string;
  readonly codecs?: ZLinkChannelEnvelopeCodecRegistry;
  readonly dispatchErrors: ZLinkDispatchErrorReporter;
  readonly handlers: readonly ZLinkRouteHandlerRegistration[];
  readonly filters?: readonly ZLinkHandlerFilter[];
  readonly spotRouteBridge?: ZLinkBackendSpotRouteBridge;
  readonly rawBridgeReplyHandler?: (received: {
    readonly parts: readonly Message[];
    readonly routingId: unknown;
    readonly spotRid?: unknown;
    readonly requestSeq: bigint | null;
  }) => boolean;
}

function collectRouteChannelHandlers(routeChannel: ZLinkRouteChannelOptions): ZLinkRouteHandlerRegistration[] {
  return [
    ...(routeChannel.handlers ?? []),
    ...(routeChannel.sendHandlers ?? []).map((handler): ZLinkRouteHandlerRegistration => ({
      kind: 'send',
      packetName: handler.packetName,
      handler: handler.handler
    })),
    ...(routeChannel.requestHandlers ?? []).map((handler): ZLinkRouteHandlerRegistration => ({
      kind: 'request',
      packetName: handler.packetName,
      handler: handler.handler
    }))
  ];
}

export class ZLinkRoutePacketDispatcher {
  private readonly sendHandlers = new Map<string, ZLinkRouteRuntimeSendHandler>();
  private readonly requestHandlers = new Map<string, ZLinkRouteRuntimeRequestHandler>();
  private readonly codecs?: ZLinkChannelEnvelopeCodecRegistry;
  private readonly dispatchErrors: ZLinkDispatchErrorReporter;
  private readonly filters: readonly ZLinkHandlerFilter[];

  constructor(options: ZLinkRoutePacketDispatcherOptions) {
    this.routerChannelId = options.routerChannelId;
    this.codecs = options.codecs;
    this.dispatchErrors = options.dispatchErrors;
    this.filters = options.filters ?? [];
    this.spotRouteBridge = options.spotRouteBridge;
    this.rawBridgeReplyHandler = options.rawBridgeReplyHandler;
    for (const handler of options.handlers) {
      const target = handler.kind === 'send' ? this.sendHandlers : this.requestHandlers;
      if (target.has(handler.packetName)) {
        throw new ZLinkConfigurationException(`Duplicate routed handler '${options.routerChannelId}:${handler.kind}:${handler.packetName}'.`);
      }
      target.set(handler.packetName, handler.handler as never);
    }
  }

  private readonly routerChannelId: string;
  private readonly spotRouteBridge?: ZLinkBackendSpotRouteBridge;
  private readonly rawBridgeReplyHandler?: ZLinkRoutePacketDispatcherOptions['rawBridgeReplyHandler'];

  private traceRouteFlow(
    outcome: ZLinkMessageFlowOutcome,
    messageKind: ZLinkDispatchMessageKind,
    packetName: string,
    correlationId: string | undefined,
    sourceRid: string
  ): void {
    const flow = this.dispatchErrors.flow;
    if (flow.enabled(outcome)) {
      flow.trace({
        outcome,
        surface: ZLinkDispatchErrorSurface.RouteMeshChannel,
        messageKind,
        packetName,
        channelName: this.routerChannelId,
        correlationId,
        sourceRid
      });
    }
  }

  async dispatch(received: {
    parts: readonly Message[];
    routingId: unknown;
    spotRid?: unknown;
    requestSeq: bigint | null;
    send?: () => ZLinkMultipartOperation<ZLinkMultipartSubmitOperation>;
  }, router: {
    reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
  }): Promise<boolean | void> {
    if (this.spotRouteBridge !== undefined) {
      const processed = this.spotRouteBridge.handleRouterReceived(
        this.routerChannelId,
        received.routingId as RoutingId,
        received.requestSeq ?? 0n,
        received.parts
      );
      if (processed) {
        return true;
      }
    }
    if (received.spotRid !== null && received.spotRid !== undefined) {
      if (received.send === undefined) {
        throw new ZLinkConfigurationException('Routed SPOT packet is missing a local SPOT delivery context.');
      }
      appendParts(received.send(), received.parts).submit();
      return true;
    }
    if (this.rawBridgeReplyHandler?.(received) === true) {
      return true;
    }
    if (received.parts.length === 0 || received.parts[0].data().length === 0) {
      return;
    }
    const envelope = decodeChannelEnvelope(received.parts);
    const packetName = envelope.packetName;
    if (packetName === undefined) {
      throw new ZLinkConfigurationException('Route packet is missing packetName.');
    }

    const routeCorr = envelope.header.correlationId ?? undefined;
    const routeSource = String(received.routingId);
    if (envelope.header.kind === ZLinkChannelMessageKind.Command) {
      this.traceRouteFlow(ZLinkMessageFlowOutcome.Received, ZLinkDispatchMessageKind.Send, packetName, routeCorr, routeSource);
      const handler = this.sendHandlers.get(packetName);
      if (handler === undefined) {
        this.dispatchErrors.report({
          surface: ZLinkDispatchErrorSurface.RouteMeshChannel,
          messageKind: ZLinkDispatchMessageKind.Send,
          reason: ZLinkDispatchErrorReason.HandlerMissing,
          action: ZLinkDispatchErrorAction.Drop,
          packetName,
          channelName: this.routerChannelId,
          sourceRid: routeSource,
          correlationId: routeCorr
        });
        return;
      }
      try {
        const codecs = codecsForFrameworkPacket(packetName, this.codecs);
        const payload = decodeChannelPayload(envelope, codecs);
        const context = this.createRouteContext(packetName, received.routingId);
        await invokeZLinkHandlerFilters(
          this.filters,
          { message: payload, context, channelName: this.routerChannelId, packetName },
          () => Promise.resolve(handler.handle(payload, context))
        );
        this.traceRouteFlow(ZLinkMessageFlowOutcome.Dispatched, ZLinkDispatchMessageKind.Send, packetName, routeCorr, routeSource);
      } catch (error) {
        this.dispatchErrors.report({
          surface: ZLinkDispatchErrorSurface.RouteMeshChannel,
          messageKind: ZLinkDispatchMessageKind.Send,
          reason: ZLinkDispatchErrorReason.HandlerException,
          action: ZLinkDispatchErrorAction.Drop,
          packetName,
          channelName: this.routerChannelId,
          sourceRid: String(received.routingId),
          correlationId: envelope.header.correlationId ?? undefined,
          error
        });
      }
      return;
    }

    if (envelope.header.kind !== ZLinkChannelMessageKind.Request) {
      return;
    }

    if (received.requestSeq === null) {
      this.dispatchErrors.report({
        surface: ZLinkDispatchErrorSurface.RouteMeshChannel,
        messageKind: ZLinkDispatchMessageKind.Request,
        reason: ZLinkDispatchErrorReason.ReplyPathMissing,
        action: ZLinkDispatchErrorAction.Drop,
        packetName,
        channelName: this.routerChannelId,
        sourceRid: String(received.routingId),
        correlationId: envelope.header.correlationId ?? undefined
      });
      throw new ZLinkConfigurationException('Route request cannot be replied to because requestSeq is missing.');
    }

    this.traceRouteFlow(ZLinkMessageFlowOutcome.Received, ZLinkDispatchMessageKind.Request, packetName, routeCorr, routeSource);
    const handler = this.requestHandlers.get(packetName);
    if (handler === undefined) {
      appendParts(
        router.reply(received.routingId, received.requestSeq),
        encodeChannelErrorReplyParts(envelope.header, `No routed request handler is registered for '${this.routerChannelId}:${packetName}'.`)
      ).submit();
      this.dispatchErrors.report({
        surface: ZLinkDispatchErrorSurface.RouteMeshChannel,
        messageKind: ZLinkDispatchMessageKind.Request,
        reason: ZLinkDispatchErrorReason.HandlerMissing,
        action: ZLinkDispatchErrorAction.ReplyError,
        packetName,
        channelName: this.routerChannelId,
        sourceRid: routeSource,
        correlationId: routeCorr
      });
      return;
    }

    try {
      const codecs = codecsForFrameworkPacket(packetName, this.codecs);
      const payload = decodeChannelPayload(envelope, codecs);
      const context = this.createRouteContext(packetName, received.routingId, received.requestSeq);
      const reply = await invokeZLinkHandlerFilters(
        this.filters,
        { message: payload, context, channelName: this.routerChannelId, packetName },
        () => Promise.resolve(handler.handle(payload, context))
      );
      appendParts(
        router.reply(received.routingId, received.requestSeq),
        encodeChannelReplyParts(envelope.header, reply, codecs)
      ).submit();
      this.traceRouteFlow(ZLinkMessageFlowOutcome.Replied, ZLinkDispatchMessageKind.Request, packetName, routeCorr, routeSource);
    } catch (error) {
      appendParts(
        router.reply(received.routingId, received.requestSeq),
        encodeChannelErrorReplyParts(envelope.header, error instanceof Error ? error.message : String(error))
      ).submit();
      this.dispatchErrors.report({
        surface: ZLinkDispatchErrorSurface.RouteMeshChannel,
        messageKind: ZLinkDispatchMessageKind.Request,
        reason: ZLinkDispatchErrorReason.HandlerException,
        action: ZLinkDispatchErrorAction.ReplyError,
        packetName,
        channelName: this.routerChannelId,
        sourceRid: String(received.routingId),
        correlationId: envelope.header.correlationId ?? undefined,
        error
      });
    }
  }

  private createRouteContext(packetName: string, sourceRid: unknown): ZLinkRouteSendContext;
  private createRouteContext(packetName: string, sourceRid: unknown, requestSeq: bigint): ZLinkRouteRequestContext;
  private createRouteContext(
    packetName: string,
    sourceRid: unknown,
    requestSeq?: bigint
  ): ZLinkRouteSendContext | ZLinkRouteRequestContext {
    const sourceNodeRid = String(sourceRid ?? '');
    if (requestSeq === undefined) {
      return {
        channelName: this.routerChannelId,
        packetName,
        contentType: JSON_CONTENT_TYPE,
        sourceNodeRid,
        sourcePeerRid: sourceNodeRid
      };
    }
    return {
      channelName: this.routerChannelId,
      packetName,
      contentType: JSON_CONTENT_TYPE,
      sourceNodeRid,
      sourcePeerRid: sourceNodeRid,
      requestSeq
    };
  }
}

export class ZLinkRouteReceiveLoop {
  private stopped = false;

  constructor(
    private readonly router: ZLinkBackendRouterSocket & {
      reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
    },
    private readonly dispatcher: ZLinkRoutePacketDispatcher
  ) {}

  async run(signal?: AbortSignal): Promise<void> {
    while (!this.stopped && signal?.aborted !== true) {
      const received = this.router.recv(1);
      if (received === undefined) {
        await new Promise<void>((resolve) => setImmediate(resolve));
        continue;
      }
      let closeReceived = true;
      try {
        const consumed = await this.dispatcher.dispatch(received, this.router);
        if (consumed === true) {
          closeReceived = false;
        }
      } finally {
        if (closeReceived) {
          received.close();
        }
      }
    }
  }

  stop(): void {
    this.stopped = true;
  }
}

export class DefaultZLinkChannelClient implements ZLinkChannelClient {
  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly transport?: ZLinkChannelClientTransport
  ) {}

  send(message: unknown): ZLinkSendCall {
    return this.sendInternal('', message);
  }

  request(request: unknown): ZLinkRequestCall {
    return this.requestInternal('', request);
  }

  sendToChannel(channelName: string, message: unknown): ZLinkSendCall {
    return this.sendInternal(channelName, message);
  }

  requestToChannel(channelName: string, request: unknown): ZLinkRequestCall {
    return this.requestInternal(channelName, request);
  }

  private sendInternal(channelName: string, message: unknown): ZLinkSendCall {
    return new DefaultZLinkSendCall(
      () => this.requireClientChannel(channelName),
      (packetName, signal) => this.requireTransport().send(channelName, packetName, message, signal)
    );
  }

  private requestInternal(channelName: string, request: unknown): ZLinkRequestCall {
    return new DefaultZLinkRequestCall(
      () => this.requireClientChannel(channelName),
      (packetName, timeoutMs, signal) => this.requireTransport().request(channelName, packetName, request, timeoutMs, signal),
      this.defaultRequestTimeout(channelName)
    );
  }

  private defaultRequestTimeout(channelName: string): number {
    return this.registration.channels.get(channelName)?.requestTimeoutMs
      ?? this.registration.requestTimeoutMs
      ?? 30_000;
  }

  private requireClientChannel(channelName: string): void {
    if (!this.registration.channelClients.has(channelName)) {
      throw new ZLinkConfigurationException(`Channel '${channelName}' does not have a client capability.`);
    }
  }

  private requireTransport(): ZLinkChannelClientTransport {
    if (this.transport === undefined) {
      throw new ZLinkConfigurationException('Channel runtime is not started.');
    }
    return this.transport;
  }
}

interface ZLinkMultipartOperation<TNext> {
  message(message: MessageLike): TNext;
}

interface ZLinkMultipartSubmitOperation extends ZLinkMultipartOperation<ZLinkMultipartSubmitOperation> {
  submit(): unknown;
}

type ZLinkMultipartReplyOperation = ZLinkMultipartSubmitOperation;

function looksLikeRawSpotRouteBridgeReply(parts: readonly Message[]): boolean {
  if (parts.length === 0) {
    return false;
  }
  try {
    const decoded = JSON.parse(parts[0].data().toString('utf8')) as unknown;
    return typeof decoded === 'object' &&
      decoded !== null &&
      (
        'ok' in decoded ||
        'response' in decoded ||
        'error' in decoded ||
        'actorPacketTarget' in decoded
      );
  } catch {
    return false;
  }
}

function appendParts<TNext extends ZLinkMultipartOperation<TNext>>(
  operation: ZLinkMultipartOperation<TNext>,
  parts: readonly MessageLike[]
): TNext {
  if (parts.length === 0) {
    throw new ZLinkConfigurationException('Channel multipart envelope must contain at least one part.');
  }
  let current: TNext = operation.message(parts[0]);
  for (let index = 1; index < parts.length; index++) {
    current = current.message(parts[index]);
  }
  return current;
}

function submitRequestOperation(operation: {
  submit(callback: (result: number, parts: readonly Message[]) => void): boolean;
}): Promise<readonly Message[]> {
  return new Promise((resolve, reject) => {
    const accepted = operation.submit((result, parts) => {
      if (result !== 0) {
        reject(new Error(`Channel request failed with result ${result}.`));
        return;
      }
      resolve(parts);
    });
    if (!accepted) {
      reject(new Error('Channel request submit was not accepted.'));
    }
  });
}

export class DefaultZLinkFanoutClient implements ZLinkFanoutClient {
  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly transport?: ZLinkChannelClientTransport
  ) {}

  publish(topic: string, event: unknown): ZLinkPublishCall {
    return this.publishInternal(this.defaultPublisherChannel(), topic, event);
  }

  publishToChannel(channelName: string, topic: string, event: unknown): ZLinkPublishCall {
    return this.publishInternal(channelName, topic, event);
  }

  private publishInternal(channelName: string, topic: string, event: unknown): ZLinkPublishCall {
    return new DefaultZLinkPublishCall(
      () => this.requirePublisherChannel(channelName),
      (packetName, signal) => this.requireTransport().publish(channelName, topic, packetName, event, signal)
    );
  }

  private requirePublisherChannel(channelName: string): void {
    if (!this.registration.fanoutPublishers.has(channelName)) {
      throw new ZLinkConfigurationException(`Channel '${channelName}' does not have a publisher capability.`);
    }
  }

  private defaultPublisherChannel(): string {
    if (this.registration.fanoutPublishers.size === 1) {
      return [...this.registration.fanoutPublishers][0];
    }
    if (this.registration.fanoutPublishers.size === 0) {
      return '';
    }
    throw new ZLinkConfigurationException('Publish channel must be specified when more than one publisher channel is registered.');
  }

  private requireTransport(): ZLinkChannelClientTransport {
    if (this.transport === undefined) {
      throw new ZLinkConfigurationException('Channel runtime is not started.');
    }
    return this.transport;
  }
}

export class DefaultZLinkRouteClient implements ZLinkRouteClient {
  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly transport?: ZLinkRouteClientTransport
  ) {}

  send(routerChannelId: string, targetNodeRid: string, message: unknown): ZLinkSendCall {
    return new DefaultZLinkSendCall(
      () => this.requireRouteChannel(routerChannelId),
      (packetName, signal) => this.requireTransport().send(routerChannelId, targetNodeRid, packetName, message, signal)
    );
  }

  request(routerChannelId: string, targetNodeRid: string, request: unknown): ZLinkRequestCall {
    return new DefaultZLinkRequestCall(
      () => this.requireRouteChannel(routerChannelId),
      (packetName, timeoutMs, signal) => this.requireTransport().request(routerChannelId, targetNodeRid, packetName, request, timeoutMs, signal),
      this.defaultRequestTimeout(routerChannelId)
    );
  }

  private defaultRequestTimeout(routerChannelId: string): number {
    return this.registration.routeChannelOptions.get(routerChannelId)?.requestTimeoutMs
      ?? this.registration.requestTimeoutMs
      ?? 30_000;
  }

  private requireRouteChannel(routerChannelId: string): void {
    if (!this.registration.routeChannels.has(routerChannelId)) {
      throw new ZLinkConfigurationException(`Route channel '${routerChannelId}' is not registered.`);
    }
  }

  private requireTransport(): ZLinkRouteClientTransport {
    if (this.transport === undefined) {
      throw new ZLinkConfigurationException('Route channel runtime is not started.');
    }
    return this.transport;
  }
}

export class DefaultZLinkSpotPublisherClient implements ZLinkSpotPublisherClient {
  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly transport?: ZLinkSpotPublisherClientTransport
  ) {}

  publishSpot(channelName: string, topic: string, event: unknown): ZLinkPublishCall {
    const resolvedChannelName = channelName.length === 0 ? this.defaultSpotPublisherChannel() : channelName;
    return new DefaultZLinkPublishCall(
      () => this.requireSpotPublisherChannel(resolvedChannelName),
      (packetName, signal) => this.requireTransport().publishSpot(resolvedChannelName, topic, packetName, event, signal)
    );
  }

  private requireSpotPublisherChannel(channelName: string): void {
    if (!this.registration.spotPublisherClients.has(channelName)) {
      throw new ZLinkConfigurationException(`SPOT publisher channel '${channelName}' is not attached.`);
    }
  }

  private defaultSpotPublisherChannel(): string {
    if (this.registration.spotPublisherClients.size === 1) {
      return [...this.registration.spotPublisherClients][0];
    }
    if (this.registration.spotPublisherClients.size === 0) {
      return '';
    }
    throw new ZLinkConfigurationException('SPOT publisher channel must be specified when more than one channel is attached.');
  }

  private requireTransport(): ZLinkSpotPublisherClientTransport {
    if (this.transport === undefined) {
      throw new ZLinkConfigurationException('SPOT publisher runtime is not started.');
    }
    return this.transport;
  }
}

class DefaultZLinkSendCall implements ZLinkSendCall {
  private packet?: string;

  constructor(
    private readonly validate: () => void,
    private readonly submitter: (packetName: string | undefined, signal?: AbortSignal) => Promise<void>
  ) {}

  packetName(packetName: string): this {
    this.packet = packetName;
    return this;
  }

  async submit(signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    this.validate();
    await this.submitter(this.packet, signal);
  }
}

class DefaultZLinkRequestCall implements ZLinkRequestCall {
  private packet?: string;
  private timeoutMs?: number;

  constructor(
    private readonly validate: () => void,
    private readonly submitter: <TReply>(
      packetName: string | undefined,
      timeoutMs: number | undefined,
      signal?: AbortSignal
    ) => Promise<TReply>,
    private readonly defaultRequestTimeoutMs?: number
  ) {}

  packetName(packetName: string): this {
    this.packet = packetName;
    return this;
  }

  timeout(timeoutMs: number): this {
    this.timeoutMs = timeoutMs;
    return this;
  }

  async submit<TReply>(signal?: AbortSignal): Promise<TReply> {
    throwIfAborted(signal);
    this.validate();
    return this.submitter<TReply>(this.packet, this.timeoutMs ?? this.defaultRequestTimeoutMs, signal);
  }
}

class DefaultZLinkPublishCall implements ZLinkPublishCall {
  private packet?: string;

  constructor(
    private readonly validate: () => void,
    private readonly submitter: (packetName: string | undefined, signal?: AbortSignal) => Promise<void>
  ) {}

  packetName(packetName: string): this {
    this.packet = packetName;
    return this;
  }

  async submit(signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    this.validate();
    await this.submitter(this.packet, signal);
  }
}

function codecsForFrameworkPacket(
  packetName: string | undefined,
  codecs: ZLinkChannelEnvelopeCodecRegistry | undefined
): ZLinkChannelEnvelopeCodecRegistry | undefined {
  return packetName?.startsWith('__zlink.') === true ? undefined : codecs;
}

function isTransientRouteNotReadyError(error: unknown): boolean {
  const message = error instanceof Error ? error.message : String(error);
  return message.includes('Host unreachable') ||
    message.includes('not ready') ||
    message.includes('async submit timed out');
}

function delay(milliseconds: number, signal: AbortSignal | undefined): Promise<void> {
  throwIfAborted(signal);
  return new Promise((resolve, reject) => {
    let abort: (() => void) | undefined;
    const timeout = setTimeout(() => {
      if (abort !== undefined) {
        signal?.removeEventListener('abort', abort);
      }
      resolve();
    }, milliseconds);
    if (signal === undefined) {
      return;
    }
    abort = () => {
      clearTimeout(timeout);
      reject(new Error('The operation was aborted.'));
    };
    signal.addEventListener('abort', abort, { once: true });
  });
}

function throwIfAborted(signal: AbortSignal | undefined): void {
  if (signal?.aborted === true) {
    throw new Error('The operation was aborted.');
  }
}
