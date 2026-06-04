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
  ZLinkRouteClient,
  ZLinkSendCall,
  ZLinkSpotRemoteAddress,
  ZLinkSpotPublisherClient
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
  ZLinkBackendContext,
  ZLinkBackendDealerSocket,
  ZLinkBackendPublisherSocket,
  ZLinkBackendRouterSocket,
  ZLinkBackendSubscriberSocket,
  ZLinkChannelBackendAdapter
} from '../backend/contracts';
import type { ZLinkRuntimeTaskRunner } from '../execution';
import { ZLinkAsyncSubmitter } from '../messaging';
import {
  closeMessages,
  decodeChannelEnvelope,
  decodeChannelReply,
  encodeChannelEnvelopeParts,
  encodeChannelErrorReplyParts,
  encodeChannelReplyParts,
  JSON_CONTENT_TYPE,
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
  private readonly channelReceiveLoops: ZLinkChannelReceiveLoop[] = [];
  private readonly subscriberReceiveLoops: ZLinkSubscriberReceiveLoop[] = [];
  private readonly routeReceiveLoops: ZLinkRouteReceiveLoop[] = [];
  private readonly sockets: ZLinkChannelSocketRegistry;

  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly adapter: ZLinkChannelBackendAdapter,
    private readonly context: ZLinkBackendContext
  ) {
    this.sockets = new ZLinkChannelSocketRegistry(registration, adapter, context);
  }

  start(taskRunner?: ZLinkRuntimeTaskRunner): Promise<void>[] {
    const tasks: Promise<void>[] = [];
    for (const [channelName, channel] of this.registration.channels) {
      if (channel.server?.bind === undefined || (channel.requestHandlers ?? []).length === 0) {
        continue;
      }
      if (taskRunner === undefined) {
        throw new ZLinkConfigurationException(`Channel '${channelName}' handler dispatch requires a runtime task runner.`);
      }
      const router = this.sockets.channelRouter(channelName);
      const dispatcher = new ZLinkChannelRequestDispatcher({
        channelName,
        handlers: new Map(channel.requestHandlers?.map((handler) => [handler.packetName, handler.handler]))
      });
      const loop = new ZLinkChannelReceiveLoop(router, dispatcher);
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
      const subscriber = this.sockets.subscriber(channelName);
      const dispatcher = new ZLinkChannelPublishDispatcher({
        channelName,
        handlers: new Map(channel.publishHandlers?.map((handler) => [handler.packetName, handler.handler]))
      });
      const loop = new ZLinkSubscriberReceiveLoop(this.adapter, subscriber, dispatcher);
      this.subscriberReceiveLoops.push(loop);
      tasks.push(taskRunner.run(`subscriber:${channelName}`, (signal) => loop.run(signal)));
    }
    for (const routeChannel of this.registration.routeChannelOptions.values()) {
      if (routeChannel.bind !== undefined) {
          const router = this.sockets.routeRouter(routeChannel.routerChannelId);
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
    const dealer = this.sockets.clientDealer(channelName);
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Command, channelName, packetName, message) as readonly Message[];
    await this.sockets.requireSubmitter(dealer).submitCommand(
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
    const dealer = this.sockets.clientDealer(channelName);
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Request, channelName, packetName, request, timeoutMs) as readonly Message[];
    return this.sockets.requireSubmitter(dealer).submitRequest(
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
    const publisher = this.sockets.publisher(channelName);
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Publish, channelName, packetName, event, undefined, topic) as readonly Message[];
    await this.sockets.requireSubmitter(publisher).submitCommand(
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
    const router = this.sockets.routeRouter(routerChannelId);
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Command, routerChannelId, packetName, message) as readonly Message[];
    await this.sockets.requireSubmitter(router).submitCommand(
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
    const router = this.sockets.routeRouter(routerChannelId);
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Request, routerChannelId, packetName, request, timeoutMs) as readonly Message[];
    return this.sockets.requireSubmitter(router).submitRequest(
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
    const router = this.sockets.routeRouter(remoteAddress.routerChannelId);
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Command, remoteAddress.routerChannelId, packetName, message) as readonly Message[];
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
    const router = this.sockets.routeRouter(remoteAddress.routerChannelId);
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Request, remoteAddress.routerChannelId, packetName, request, timeoutMs) as readonly Message[];
    return this.sockets.requireSubmitter(router).submitRequest(
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
    const channelLoops = [...this.channelReceiveLoops];
    const subscriberLoops = [...this.subscriberReceiveLoops];
    const loops = [...this.routeReceiveLoops];
    this.channelReceiveLoops.length = 0;
    this.subscriberReceiveLoops.length = 0;
    this.routeReceiveLoops.length = 0;
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
    await this.sockets.dispose();
  }
}

class ZLinkChannelSocketRegistry {
  private readonly clientDealers = new Map<string, ZLinkBackendDealerSocket>();
  private readonly channelRouters = new Map<string, ZLinkBackendRouterSocket>();
  private readonly publishers = new Map<string, ZLinkBackendPublisherSocket>();
  private readonly subscribers = new Map<string, ZLinkBackendSubscriberSocket>();
  private readonly routeRouters = new Map<string, ZLinkBackendRouterSocket>();
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
      ...this.routeRouters.values()
    ];
    this.clientDealers.clear();
    this.channelRouters.clear();
    this.publishers.clear();
    this.subscribers.clear();
    this.routeRouters.clear();
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
    const client = channel?.client ?? channel?.dealerMesh?.client;
    if (client === undefined) {
      throw new ZLinkConfigurationException(`Channel client '${channelName}' is not registered.`);
    }

    const dealer = this.adapter.createDealerSocket(this.context);
    dealer.setChannelName(channelName);
    this.trackSubmitter(dealer);
    for (const endpoint of client.manualConnections ?? []) {
      dealer.connect(endpoint);
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
    router.bind(channel.server.bind);
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
    for (const endpoint of channel.subscriber.manualConnections ?? []) {
      subscriber.connect(endpoint);
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

export interface ZLinkChannelRequestDispatcherOptions {
  readonly channelName: string;
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

    const context: ZLinkHandlerContext = { channelName: this.options.channelName, packetName };
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

export class ZLinkChannelReceiveLoop {
  private stopped = false;

  constructor(
    private readonly router: ZLinkBackendRouterSocket & {
      reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
    },
    private readonly dispatcher: ZLinkChannelRequestDispatcher
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

export interface ZLinkChannelPublishDispatcherOptions {
  readonly channelName: string;
  readonly handlers: ReadonlyMap<string, ZLinkRuntimePublishHandler>;
  readonly filters?: readonly ZLinkHandlerFilter[];
}

export interface ZLinkRuntimePublishHandler {
  handle(payload: Buffer, context: ZLinkPublishContext): Promise<void> | void;
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
    const handler = this.options.handlers.get(packetName);
    if (handler === undefined) {
      return;
    }

    const context: ZLinkPublishContext = {
      channelName: this.options.channelName,
      packetName,
      contentType: envelope.header.contentType,
      topic: envelope.header.topic ?? topicMessage.topic,
      source: envelope.header.source ?? undefined
    };
    await invokeZLinkHandlerFilters(
      this.filters,
      { context, handler },
      () => Promise.resolve(handler.handle(envelope.payload, context))
    );
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
    while (!this.stopped && !signal?.aborted) {
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
    private readonly transport?: ZLinkSpotPublisherClientTransport
  ) {}

  publishSpot<TEvent>(channelName: string, topic: string, event: TEvent): ZLinkPublishCall {
    return new DefaultZLinkPublishCall(
      () => this.requireSpotPublisherChannel(channelName),
      (packetName, signal) => this.requireTransport().publishSpot(channelName, topic, packetName, event, signal)
    );
  }

  private requireSpotPublisherChannel(channelName: string): void {
    if (!this.registration.spotPublisherClients.has(channelName)) {
      throw new ZLinkConfigurationException(`SPOT publisher channel '${channelName}' is not attached.`);
    }
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
