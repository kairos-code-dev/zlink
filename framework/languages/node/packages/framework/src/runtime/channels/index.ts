import type {
  ZLinkChannelClient,
  ZLinkFanoutClient,
  ZLinkHandlerContext,
  ZLinkHandlerFilter,
  ZLinkPublishCall,
  ZLinkRouteRequestContext,
  ZLinkRouteSendContext,
  ZLinkRequestCall,
  ZLinkRouteClient,
  ZLinkSendCall,
  ZLinkSpotRemoteAddress,
  ZLinkSpotPublisherClient
} from '../../contracts';
import { randomUUID } from 'node:crypto';
import { invokeZLinkHandlerFilters } from '../handlers';
import type {
  DealerSocket,
  Message,
  MessageLike,
  PubSocket
} from '@zlink-systems/zlink';
import { ZLinkConfigurationException, type ZLinkFrameworkRegistration, type ZLinkRouteChannelOptions } from '../configuration';
import type {
  ZLinkBackendContext,
  ZLinkBackendDealerSocket,
  ZLinkBackendPublisherSocket,
  ZLinkBackendRouterSocket,
  ZLinkChannelBackendAdapter
} from '../backend/contracts';
import type { ZLinkRuntimeTaskRunner } from '../execution';
import { ZLinkAsyncSubmitter } from '../messaging';

const JSON_CONTENT_TYPE = 'application/json';

const enum ZLinkChannelMessageKind {
  Request = 1,
  Response = 2,
  Command = 3,
  Publish = 4,
  Error = 5
}

interface ZLinkChannelEnvelopeHeader {
  readonly kind: ZLinkChannelMessageKind;
  readonly channelName: string;
  readonly messageName: string;
  readonly contentType: string;
  readonly correlationId: string | null;
  readonly deadline: string | null;
  readonly topic: string | null;
  readonly errorCode: string | null;
  readonly errorMessage: string | null;
  readonly source?: string | null;
}

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
  constructor(private readonly manager: () => ZLinkChannelRuntimeManager | undefined) {}

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

  async requestToSpot<TRequest, TReply = unknown>(
    remoteAddress: ZLinkSpotRemoteAddress,
    request: TRequest,
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

  private requireManager(): ZLinkChannelRuntimeManager {
    const manager = this.manager();
    if (manager === undefined) {
      throw new ZLinkConfigurationException('Route channel runtime is not started.');
    }
    return manager;
  }
}

export class ZLinkChannelRuntimeManager {
  private readonly clientDealers = new Map<string, ZLinkBackendDealerSocket>();
  private readonly publishers = new Map<string, ZLinkBackendPublisherSocket>();
  private readonly routeRouters = new Map<string, ZLinkBackendRouterSocket>();
  private readonly routeReceiveLoops: ZLinkRouteReceiveLoop[] = [];
  private readonly submitters = new WeakMap<object, ZLinkAsyncSubmitter>();
  private readonly ownedSubmitters = new Set<ZLinkAsyncSubmitter>();

  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly adapter: ZLinkChannelBackendAdapter,
    private readonly context: ZLinkBackendContext
  ) {}

  start(taskRunner?: ZLinkRuntimeTaskRunner): Promise<void>[] {
    const tasks: Promise<void>[] = [];
    for (const routeChannel of this.registration.routeChannelOptions.values()) {
      if (routeChannel.bind !== undefined) {
        const router = this.getOrCreateRouteRouter(routeChannel.routerChannelId);
        const handlers = collectRouteChannelHandlers(routeChannel);
        if (handlers.length > 0) {
          if (taskRunner === undefined) {
            throw new ZLinkConfigurationException(`Route channel '${routeChannel.routerChannelId}' handler dispatch requires a runtime task runner.`);
          }
          const dispatcher = new ZLinkRoutePacketDispatcher({
            routerChannelId: routeChannel.routerChannelId,
            handlers
          });
          const loop = new ZLinkRouteReceiveLoop(router, dispatcher);
          this.routeReceiveLoops.push(loop);
          tasks.push(taskRunner.run(`route:${routeChannel.routerChannelId}`, (signal) => loop.run(signal)));
        }
      }
    }
    return tasks;
  }

  async send(channelName: string, packetName: string | undefined, message: unknown, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    const dealer = this.getOrCreateClientDealer(channelName);
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Command, channelName, packetName, message) as readonly Message[];
    await this.requireSubmitter(dealer).submitCommand(
      () => dealer.send(parts, 0),
      signal
    );
  }

  async request<TReply>(
    channelName: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> {
    throwIfAborted(signal);
    const dealer = this.getOrCreateClientDealer(channelName);
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Request, channelName, packetName, request, timeoutMs) as readonly Message[];
    return this.requireSubmitter(dealer).submitRequest(
      (resolve, reject) => dealer.request(
        parts,
        (result, parts) => {
          try {
            if (result !== 0) {
              reject(new ZLinkConfigurationException(`Channel '${channelName}' request failed with result ${result}.`));
              return;
            }
            resolve(decodeChannelReply<TReply>(parts as readonly Message[]));
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
    const publisher = this.getOrCreatePublisher(channelName);
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Publish, channelName, packetName, event, undefined, topic) as readonly Message[];
    await this.requireSubmitter(publisher).submitCommand(
      () => publisher.publish(
        topic,
        parts,
        0
      ),
      signal
    );
  }

  async routeSend(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    const router = this.getOrCreateRouteRouter(routerChannelId);
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Command, routerChannelId, packetName, message) as readonly Message[];
    await this.requireSubmitter(router).submitCommand(
      () => router.send(targetNodeRid, parts, 0),
      signal
    );
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
    const router = this.getOrCreateRouteRouter(routerChannelId);
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Request, routerChannelId, packetName, request, timeoutMs) as readonly Message[];
    return this.requireSubmitter(router).submitRequest(
      (resolve, reject) => router.request(
        targetNodeRid,
        parts,
        (result, parts) => {
          try {
            if (result !== 0) {
              reject(new ZLinkConfigurationException(`Route channel '${routerChannelId}' request failed with result ${result}.`));
              return;
            }
            resolve(decodeChannelReply<TReply>(parts as readonly Message[]));
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

  async routeSendToSpot(
    remoteAddress: ZLinkSpotRemoteAddress,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    const router = this.getOrCreateRouteRouter(remoteAddress.routerChannelId);
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Command, remoteAddress.routerChannelId, packetName, message) as readonly Message[];
    await this.requireSubmitter(router).submitCommand(
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
    const router = this.getOrCreateRouteRouter(remoteAddress.routerChannelId);
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Request, remoteAddress.routerChannelId, packetName, request, timeoutMs) as readonly Message[];
    return this.requireSubmitter(router).submitRequest(
      (resolve, reject) => router.requestToSpot(
        remoteAddress.targetNodeRid,
        remoteAddress.spotRid,
        parts,
        (result, parts) => {
          try {
            if (result !== 0) {
              reject(new ZLinkConfigurationException(`Route channel '${remoteAddress.routerChannelId}' spot request failed with result ${result}.`));
              return;
            }
            resolve(decodeChannelReply<TReply>(parts as readonly Message[]));
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

  async dispose(): Promise<void> {
    const sockets = [...this.clientDealers.values(), ...this.publishers.values(), ...this.routeRouters.values()];
    const loops = [...this.routeReceiveLoops];
    this.clientDealers.clear();
    this.publishers.clear();
    this.routeRouters.clear();
    this.routeReceiveLoops.length = 0;
    for (const loop of loops) {
      loop.stop();
    }
    for (const submitter of this.ownedSubmitters) {
      submitter.dispose();
    }
    this.ownedSubmitters.clear();
    await new Promise<void>((resolve) => setImmediate(resolve));
    await Promise.all(sockets.map((socket) => socket.dispose()));
  }

  private getOrCreateClientDealer(channelName: string): ZLinkBackendDealerSocket {
    const existing = this.clientDealers.get(channelName);
    if (existing !== undefined) {
      return existing;
    }

    const channel = this.registration.channels.get(channelName);
    if (channel?.client === undefined) {
      throw new ZLinkConfigurationException(`Channel client '${channelName}' is not registered.`);
    }

    const dealer = this.adapter.createDealerSocket(this.context);
    dealer.setChannelName(channelName);
    this.trackSubmitter(dealer);
    for (const endpoint of channel.client.manualConnections ?? []) {
      dealer.connect(endpoint);
    }
    this.clientDealers.set(channelName, dealer);
    return dealer;
  }

  private getOrCreatePublisher(channelName: string): ZLinkBackendPublisherSocket {
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

  private getOrCreateRouteRouter(routerChannelId: string): ZLinkBackendRouterSocket {
    const existing = this.routeRouters.get(routerChannelId);
    if (existing !== undefined) {
      return existing;
    }

    const routeChannel = this.registration.routeChannelOptions.get(routerChannelId);
    if (routeChannel === undefined) {
      throw new ZLinkConfigurationException(`Route channel '${routerChannelId}' is not registered.`);
    }
    if (routeChannel.bind === undefined || routeChannel.bind.trim().length === 0) {
      throw new ZLinkConfigurationException(`Route channel '${routerChannelId}' does not define a bind endpoint.`);
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
    router.bind(routeChannel.bind);
    for (const endpoint of routeChannel.manualConnections ?? []) {
      router.connect(endpoint);
    }
    this.routeRouters.set(routerChannelId, router);
    return router;
  }

  private trackSubmitter(socket: ZLinkBackendDealerSocket | ZLinkBackendPublisherSocket | ZLinkBackendRouterSocket): void {
    const submitter = new ZLinkAsyncSubmitter((handler) => socket.onSendReady(handler));
    this.submitters.set(socket, submitter);
    this.ownedSubmitters.add(submitter);
  }

  private requireSubmitter(socket: ZLinkBackendDealerSocket | ZLinkBackendPublisherSocket | ZLinkBackendRouterSocket): ZLinkAsyncSubmitter {
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

  async send(channelName: string, packetName: string | undefined, message: unknown, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    appendParts(
      this.dealer.send(),
      encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Command, channelName, packetName, message)
    ).submit();
  }

  async request<TReply>(
    channelName: string,
    packetName: string | undefined,
    request: unknown,
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
    const reply = await operation.submitAsync();
    return decodeChannelReply<TReply>(reply);
  }

  async publish(channelName: string, topic: string, packetName: string | undefined, event: unknown, signal?: AbortSignal): Promise<void> {
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

export interface ZLinkChannelEnvelope {
  readonly packetName?: string;
  readonly payload: Buffer;
}

export interface ZLinkChannelRequestDispatcherOptions {
  readonly handlers: ReadonlyMap<string, ZLinkChannelRequestHandler>;
  readonly filters?: readonly ZLinkHandlerFilter[];
}

export interface ZLinkChannelRequestHandler {
  handle(payload: Buffer, context: ZLinkHandlerContext): Promise<unknown> | unknown;
}

export interface ZLinkRouteHandlerRegistration {
  readonly kind: 'send' | 'request';
  readonly packetName: string;
  readonly handler: ZLinkRouteRuntimeSendHandler | ZLinkRouteRuntimeRequestHandler;
}

export interface ZLinkRouteRuntimeSendHandler {
  handle(payload: Buffer, context: ZLinkRouteSendContext): Promise<void> | void;
}

export interface ZLinkRouteRuntimeRequestHandler {
  handle(payload: Buffer, context: ZLinkRouteRequestContext): Promise<unknown> | unknown;
}

export class ZLinkChannelRequestDispatcher {
  private readonly filters: readonly ZLinkHandlerFilter[];

  constructor(private readonly options: ZLinkChannelRequestDispatcherOptions) {
    this.filters = options.filters ?? [];
  }

  async dispatch(received: { parts: readonly Message[]; routingId: unknown; requestSeq: bigint | null }, router: {
    reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
  }): Promise<void> {
    if (received.parts.length === 0 || received.parts[0].data().length === 0) {
      return;
    }
    const envelope = decodeChannelEnvelope(received.parts);
    const packetName = envelope.packetName;
    if (packetName === undefined) {
      throw new ZLinkConfigurationException('Channel request is missing packetName.');
    }
    const handler = this.options.handlers.get(packetName);
    if (handler === undefined) {
      throw new ZLinkConfigurationException(`No channel request handler is registered for packet '${packetName}'.`);
    }
    if (received.requestSeq === null) {
      throw new ZLinkConfigurationException('Channel request cannot be replied to because requestSeq is missing.');
    }

    const context: ZLinkHandlerContext = { packetName };
    const reply = await invokeZLinkHandlerFilters(
      this.filters,
      { context, handler },
      () => Promise.resolve(handler.handle(envelope.payload, context))
    );
    appendParts(
      router.reply(received.routingId, received.requestSeq),
      encodeChannelReplyParts(envelope.header, reply)
    ).submit();
  }
}

export interface ZLinkRoutePacketDispatcherOptions {
  readonly routerChannelId: string;
  readonly handlers: readonly ZLinkRouteHandlerRegistration[];
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

  constructor(options: ZLinkRoutePacketDispatcherOptions) {
    this.routerChannelId = options.routerChannelId;
    for (const handler of options.handlers) {
      const target = handler.kind === 'send' ? this.sendHandlers : this.requestHandlers;
      if (target.has(handler.packetName)) {
        throw new ZLinkConfigurationException(`Duplicate routed handler '${options.routerChannelId}:${handler.kind}:${handler.packetName}'.`);
      }
      target.set(handler.packetName, handler.handler as never);
    }
  }

  private readonly routerChannelId: string;

  async dispatch(received: { parts: readonly Message[]; routingId: unknown; requestSeq: bigint | null }, router: {
    reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
  }): Promise<void> {
    if (received.parts.length === 0 || received.parts[0].data().length === 0) {
      return;
    }
    const envelope = decodeChannelEnvelope(received.parts);
    const packetName = envelope.packetName;
    if (packetName === undefined) {
      throw new ZLinkConfigurationException('Route packet is missing packetName.');
    }

    if (envelope.header.kind === ZLinkChannelMessageKind.Command) {
      const handler = this.sendHandlers.get(packetName);
      if (handler === undefined) {
        return;
      }
      await handler.handle(envelope.payload, this.createSendContext(packetName, received.routingId));
      return;
    }

    if (envelope.header.kind !== ZLinkChannelMessageKind.Request) {
      return;
    }

    if (received.requestSeq === null) {
      throw new ZLinkConfigurationException('Route request cannot be replied to because requestSeq is missing.');
    }

    const handler = this.requestHandlers.get(packetName);
    if (handler === undefined) {
      appendParts(
        router.reply(received.routingId, received.requestSeq),
        encodeChannelErrorReplyParts(envelope.header, `No routed request handler is registered for '${this.routerChannelId}:${packetName}'.`)
      ).submit();
      return;
    }

    try {
      const reply = await handler.handle(
        envelope.payload,
        {
          ...this.createSendContext(packetName, received.routingId),
          requestSeq: received.requestSeq
        }
      );
      appendParts(
        router.reply(received.routingId, received.requestSeq),
        encodeChannelReplyParts(envelope.header, reply)
      ).submit();
    } catch (error) {
      appendParts(
        router.reply(received.routingId, received.requestSeq),
        encodeChannelErrorReplyParts(envelope.header, error instanceof Error ? error.message : String(error))
      ).submit();
    }
  }

  private createSendContext(packetName: string, sourceRid: unknown): ZLinkRouteSendContext {
    const sourceNodeRid = String(sourceRid ?? '');
    return {
      channelName: this.routerChannelId,
      packetName,
      contentType: JSON_CONTENT_TYPE,
      sourceNodeRid,
      sourcePeerRid: sourceNodeRid
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
    while (!this.stopped && !signal?.aborted) {
      const received = this.router.recv(1);
      if (received === undefined) {
        await new Promise<void>((resolve) => setImmediate(resolve));
        continue;
      }
      try {
        await this.dispatcher.dispatch(received, this.router);
      } finally {
        received.close();
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

  send<TMessage>(channelNameOrMessage: string | TMessage, maybeMessage?: TMessage): ZLinkSendCall {
    const channelName = typeof channelNameOrMessage === 'string' ? channelNameOrMessage : '';
    const message = typeof channelNameOrMessage === 'string' ? maybeMessage : channelNameOrMessage;
    return new DefaultZLinkSendCall(
      () => this.requireClientChannel(channelName),
      (packetName, signal) => this.requireTransport().send(channelName, packetName, message, signal)
    );
  }

  request<TRequest>(channelNameOrRequest: string | TRequest, maybeRequest?: TRequest): ZLinkRequestCall {
    const channelName = typeof channelNameOrRequest === 'string' ? channelNameOrRequest : '';
    const request = typeof channelNameOrRequest === 'string' ? maybeRequest : channelNameOrRequest;
    return new DefaultZLinkRequestCall(
      () => this.requireClientChannel(channelName),
      (packetName, timeoutMs, signal) => this.requireTransport().request(channelName, packetName, request, timeoutMs, signal)
    );
  }

  sendToChannel<TMessage>(channelName: string, message: TMessage): ZLinkSendCall {
    return this.send(channelName, message);
  }

  requestToChannel<TRequest>(channelName: string, request: TRequest): ZLinkRequestCall {
    return this.request(channelName, request);
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

interface ZLinkMultipartRequestOperation extends ZLinkMultipartOperation<ZLinkMultipartRequestOperation> {
  timeout(timeoutMs: number): ZLinkMultipartRequestOperation;
  submitAsync(): Promise<Message[]>;
}

type ZLinkMultipartReplyOperation = ZLinkMultipartSubmitOperation;

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

function toMessageLike(value: unknown): MessageLike {
  if (Buffer.isBuffer(value) || value instanceof Uint8Array || isMessage(value)) {
    return value;
  }
  return encodeJsonBytes(value);
}

function isMessage(value: unknown): value is Message {
  return typeof value === 'object' && value !== null && typeof (value as { data?: unknown }).data === 'function';
}

function encodeChannelEnvelopeParts(
  kind: ZLinkChannelMessageKind,
  channelName: string,
  packetName: string | undefined,
  payload: unknown,
  timeoutMs?: number,
  topic?: string
): readonly MessageLike[] {
  const header: ZLinkChannelEnvelopeHeader = {
    kind,
    channelName,
    messageName: packetName ?? resolveChannelPacketName(payload),
    contentType: JSON_CONTENT_TYPE,
    correlationId: randomUUID().replaceAll('-', ''),
    deadline: timeoutMs === undefined ? null : new Date(Date.now() + timeoutMs).toISOString(),
    topic: topic ?? null,
    errorCode: null,
    errorMessage: null
  };
  return [encodeJsonBytes(header), toMessageLike(payload)];
}

function encodeChannelReplyParts(request: ZLinkChannelEnvelopeHeader, payload: unknown): readonly MessageLike[] {
  const header: ZLinkChannelEnvelopeHeader = {
    kind: ZLinkChannelMessageKind.Response,
    channelName: request.channelName,
    messageName: request.messageName,
    contentType: JSON_CONTENT_TYPE,
    correlationId: request.correlationId,
    deadline: null,
    topic: null,
    errorCode: null,
    errorMessage: null
  };
  return [encodeJsonBytes(header), toMessageLike(payload ?? Buffer.alloc(0))];
}

function encodeChannelErrorReplyParts(request: ZLinkChannelEnvelopeHeader, message: string): readonly MessageLike[] {
  const header: ZLinkChannelEnvelopeHeader = {
    kind: ZLinkChannelMessageKind.Error,
    channelName: request.channelName,
    messageName: request.messageName,
    contentType: JSON_CONTENT_TYPE,
    correlationId: request.correlationId,
    deadline: null,
    topic: null,
    errorCode: 'ZLinkRouteHandlerError',
    errorMessage: message
  };
  return [encodeJsonBytes(header), encodeJsonBytes(null)];
}

function decodeChannelReply<TReply>(parts: readonly Message[]): TReply {
  const header = decodeChannelHeader(parts);
  if (header.kind === ZLinkChannelMessageKind.Error) {
    throw new ZLinkConfigurationException(header.errorMessage ?? 'ZLink channel request failed.');
  }
  if (header.kind !== ZLinkChannelMessageKind.Response) {
    throw new ZLinkConfigurationException(`Channel reply kind '${header.kind}' is not a response.`);
  }
  if (parts.length < 2 || parts[1].data().length === 0) {
    return undefined as TReply;
  }
  return JSON.parse(parts[1].data().toString()) as TReply;
}

function closeMessages(parts: readonly Message[]): void {
  for (const part of parts) {
    part.close();
  }
}

function decodeChannelEnvelope(parts: readonly Message[]): ZLinkChannelEnvelope & { readonly header: ZLinkChannelEnvelopeHeader } {
  const header = decodeChannelHeader(parts);
  if (parts.length < 2) {
    throw new ZLinkConfigurationException('Channel envelope body part is missing.');
  }
  return { header, packetName: header.messageName, payload: Buffer.from(parts[1].data()) };
}

function decodeChannelHeader(parts: readonly Message[]): ZLinkChannelEnvelopeHeader {
  if (parts.length === 0) {
    throw new ZLinkConfigurationException('Channel envelope header part is missing.');
  }
  return JSON.parse(parts[0].data().toString()) as ZLinkChannelEnvelopeHeader;
}

function encodeJsonBytes(value: unknown): Buffer {
  return Buffer.from(JSON.stringify(value ?? null));
}

function resolveChannelPacketName(payload: unknown): string {
  if (payload !== null && typeof payload === 'object' && 'constructor' in payload) {
    const name = (payload as { constructor?: { name?: string } }).constructor?.name;
    if (name !== undefined && name !== 'Object') {
      return name;
    }
  }
  throw new ZLinkConfigurationException('Channel packetName is required when the payload type cannot provide one.');
}

export class DefaultZLinkFanoutClient implements ZLinkFanoutClient {
  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly transport?: ZLinkChannelClientTransport
  ) {}

  publish<TEvent>(topicOrChannelName: string, eventOrTopic: TEvent | string, maybeEvent?: TEvent): ZLinkPublishCall {
    const channelName = maybeEvent === undefined ? '' : topicOrChannelName;
    const topic = maybeEvent === undefined ? topicOrChannelName : String(eventOrTopic);
    const event = maybeEvent === undefined ? eventOrTopic : maybeEvent;
    return new DefaultZLinkPublishCall(
      () => this.requirePublisherChannel(channelName),
      (packetName, signal) => this.requireTransport().publish(channelName, topic, packetName, event, signal)
    );
  }

  publishToChannel<TEvent>(channelName: string, topic: string, event: TEvent): ZLinkPublishCall {
    return this.publish(channelName, topic, event);
  }

  private requirePublisherChannel(channelName: string): void {
    if (!this.registration.fanoutPublishers.has(channelName)) {
      throw new ZLinkConfigurationException(`Channel '${channelName}' does not have a publisher capability.`);
    }
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

  send<TMessage>(routerChannelId: string, targetNodeRid: string, message: TMessage): ZLinkSendCall {
    return new DefaultZLinkSendCall(
      () => this.requireRouteChannel(routerChannelId),
      (packetName, signal) => this.requireTransport().send(routerChannelId, targetNodeRid, packetName, message, signal)
    );
  }

  request<TRequest>(routerChannelId: string, targetNodeRid: string, request: TRequest): ZLinkRequestCall {
    return new DefaultZLinkRequestCall(
      () => this.requireRouteChannel(routerChannelId),
      (packetName, timeoutMs, signal) => this.requireTransport().request(routerChannelId, targetNodeRid, packetName, request, timeoutMs, signal)
    );
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
    private readonly transport?: ZLinkChannelClientTransport
  ) {}

  publishSpot<TEvent>(channelName: string, topic: string, event: TEvent): ZLinkPublishCall {
    return new DefaultZLinkPublishCall(
      () => this.requireSpotPublisherChannel(channelName),
      (packetName, signal) => this.requireTransport().publish(channelName, topic, packetName, event, signal)
    );
  }

  private requireSpotPublisherChannel(channelName: string): void {
    if (!this.registration.spotPublisherClients.has(channelName)) {
      throw new ZLinkConfigurationException(`SPOT publisher channel '${channelName}' is not attached.`);
    }
  }

  private requireTransport(): ZLinkChannelClientTransport {
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
    ) => Promise<TReply>
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
    return this.submitter<TReply>(this.packet, this.timeoutMs, signal);
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

function throwIfAborted(signal: AbortSignal | undefined): void {
  if (signal?.aborted) {
    throw new Error('The operation was aborted.');
  }
}
