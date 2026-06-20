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
  ZLinkMessageDispatchErrorObserver,
  ZLinkMessageDispatchErrorEvent,
  ZLinkProviderResolver
} from '../../contracts';
import {
  ZLinkAutoConnectType,
  ZLinkDispatchErrorAction,
  ZLinkDispatchErrorReason,
  ZLinkDispatchErrorSurface,
  ZLinkDispatchMessageKind,
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
  ZLinkBackendSubscriberSocket,
  ZLinkChannelBackendAdapter
} from '../backend/contracts';
import type { ZLinkRuntimeErrorSink, ZLinkRuntimeTaskRunner } from '../execution';
import { ZLinkAsyncSubmitter } from '../messaging';
import {
  closeMessages,
  decodeChannelEnvelope,
  decodeChannelPayload,
  decodeChannelReply,
  encodeChannelEnvelopeParts,
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
  private observerFailures = 0;

  constructor(
    private readonly observerType: Type<ZLinkMessageDispatchErrorObserver> | undefined,
    private readonly providerResolver: ZLinkProviderResolver | undefined,
    private readonly errorSink: ZLinkDispatchErrorSink
  ) {}

  report(event: ZLinkMessageDispatchErrorEvent): void {
    this.reportedEvents += 1;
    this.errorSink.reportRuntimeTaskException(
      'dispatch-error',
      event.error ?? new Error(
        `ZLink message dispatch error: surface=${event.surface}, kind=${event.messageKind}, reason=${event.reason}, action=${event.action}`
      )
    );
    if (this.observerType === undefined) {
      return;
    }
    queueMicrotask(() => {
      void this.resolveObserver()
        .then((observer) => observer.onDispatchError(event))
        .catch((error) => {
          this.observerFailures += 1;
          this.errorSink.reportRuntimeTaskException('dispatch-error-observer', error);
        });
    });
  }

  get reportedCount(): number {
    return this.reportedEvents;
  }

  get observerFailureCount(): number {
    return this.observerFailures;
  }

  private async resolveObserver(): Promise<ZLinkMessageDispatchErrorObserver> {
    const observerType = this.observerType;
    if (observerType === undefined) {
      throw new Error('Dispatch error observer is not configured.');
    }
    const existing = this.providerResolver?.get?.(observerType);
    if (existing !== undefined) {
      return existing;
    }
    const created = await this.providerResolver?.create?.(observerType);
    if (created !== undefined) {
      return created;
    }
    return new observerType();
  }
}

export class ZLinkChannelRuntimeManager {
  private readonly channelReceiveLoops: ZLinkChannelReceiveLoop[] = [];
  private readonly subscriberReceiveLoops: ZLinkSubscriberReceiveLoop[] = [];
  private readonly routeReceiveLoops: Array<{ stop(): void }> = [];
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
        sendHandlers: new Map(channel.sendHandlers?.map((handler) => [handler.packetName, handler.handler]))
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
      const subscriber = this.sockets['subscriber'](channelName);
      const dispatcher = new ZLinkChannelPublishDispatcher({
        channelName,
        codecs: this.codecs,
        dispatchErrors: this.createDispatchErrorReporter(taskRunner.errorSink),
        handlers: new Map(channel.publishHandlers?.map((handler) => [handler.packetName, handler.handler]))
      });
      const loop = new ZLinkSubscriberReceiveLoop(this.adapter, subscriber, dispatcher);
      this.subscriberReceiveLoops.push(loop);
      tasks.push(taskRunner.run(`subscriber:${channelName}`, (signal) => loop.run(signal)));
    }
    for (const routeChannel of this.registration.routeChannelOptions.values()) {
      const spotRouteNode = this.spotRouteNode(routeChannel.routerChannelId);
      if (routeChannel.bind !== undefined) {
        const router = this.sockets.routeRouter(routeChannel.routerChannelId);
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
          spotRouteNode
        });
        const loop = new ZLinkRouteReceiveLoop(router, dispatcher);
        this.routeReceiveLoops.push(loop);
        tasks.push(taskRunner.run(`route:${routeChannel.routerChannelId}`, (signal) => loop.run(signal)));
      } else if (spotRouteNode !== undefined) {
        if (taskRunner === undefined) {
          throw new ZLinkConfigurationException(`Spot route channel '${routeChannel.routerChannelId}' dispatch requires a runtime task runner.`);
        }
        const spotLoop = new ZLinkSpotRouteReceiveLoop(spotRouteNode);
        this.routeReceiveLoops.push(spotLoop);
        tasks.push(taskRunner.run(`route:${routeChannel.routerChannelId}:spot`, (signal) => spotLoop.run(signal)));
      }
    }
    return tasks;
  }

  private createDispatchErrorReporter(errorSink: ZLinkRuntimeErrorSink): ZLinkDispatchErrorReporter {
    return new ZLinkDispatchErrorReporter(
      this.registration.dispatch?.messageDispatchErrorObserverType,
      this.providerResolver,
      errorSink
    );
  }

  async send(channelName: string, packetName: string | undefined, message: unknown, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    const dealer = this.sockets.clientDealer(channelName);
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Command, channelName, packetName, message, undefined, undefined, this.codecs) as readonly Message[];
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
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Request, channelName, packetName, request, timeoutMs, undefined, this.codecs) as readonly Message[];
    return this.sockets.requireSubmitter(dealer).submitRequest(
      (resolve, reject) => dealer.request(
        parts,
        (result, parts) => {
          try {
            if (result !== 0) {
              reject(new ZLinkConfigurationException(`Channel '${channelName}' request failed with result ${result}.`));
              return;
            }
            resolve(decodeChannelReply<TReply>(parts as readonly Message[], this.codecs));
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
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Publish, channelName, packetName, event, undefined, topic, this.codecs) as readonly Message[];
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
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Command, routerChannelId, packetName, message, undefined, undefined, this.codecs) as readonly Message[];
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
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Request, routerChannelId, packetName, request, timeoutMs, undefined, this.codecs) as readonly Message[];
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
              resolve(decodeChannelReply<TReply>(parts as readonly Message[], this.codecs));
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
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Command, remoteAddress.routerChannelId, packetName, message, undefined, undefined, this.codecs) as readonly Message[];
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
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Request, remoteAddress.routerChannelId, packetName, request, timeoutMs, undefined, this.codecs) as readonly Message[];
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
                resolve(decodeChannelReply<TReply>(parts as readonly Message[], this.codecs));
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
                resolve(decodeChannelReply<TReply>(replyParts as readonly Message[], this.codecs));
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
              resolve(decodeChannelReply<TReply>(parts as readonly Message[], this.codecs));
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
    if (this.hasBoundRouteRouter(remoteAddress.routerChannelId)) {
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

  private spotNodeRouter(routerChannelId: string): ZLinkBackendSpot | undefined {
    return this.spotRouteNode(routerChannelId)?.entrySpot()
      ?? this.spotNodes?.get(routerChannelId)?.entrySpot();
  }

  private spotRouteNode(routerChannelId: string): ZLinkBackendSpotNode | undefined {
    for (const [spotNodeName, spotNode] of this.registration.spotNodes) {
      if (spotNode.acceptedSpotRouteChannels?.[routerChannelId] !== undefined) {
        return this.spotNodes?.get(spotNodeName);
      }
    }
    return undefined;
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
    const effectiveTimeoutMs = timeoutMs ?? this.registration.requestTimeoutMs ?? 10000;
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
    const client = channel?.client ?? channel?.dealerMesh?.client;
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
}

export class ZLinkChannelRequestDispatcher {
  private readonly filters: readonly ZLinkHandlerFilter[];

  constructor(private readonly options: ZLinkChannelRequestDispatcherOptions) {
    this.filters = options.filters ?? [];
  }

  async dispatch(received: {
    parts: readonly Message[];
    routingId: unknown;
    spotRid?: unknown;
    requestSeq: bigint | null;
    send?: () => ZLinkMultipartOperation<ZLinkMultipartSubmitOperation>;
  }, router: {
    reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
  }): Promise<void> {
    if (received.spotRid !== null && received.spotRid !== undefined) {
      if (received.send === undefined) {
        throw new ZLinkConfigurationException('Routed SPOT packet is missing a local SPOT delivery context.');
      }
      appendParts(received.send(), received.parts).submit();
      return;
    }
    if (received.parts.length === 0 || received.parts[0].data().length === 0) {
      return;
    }
    const envelope = decodeChannelEnvelope(received.parts);
    const packetName = envelope.packetName;
    if (packetName === undefined) {
      throw new ZLinkConfigurationException('Channel packet is missing packetName.');
    }
    if (envelope.header.kind === ZLinkChannelMessageKind.Command) {
      const handler = this.options.sendHandlers?.get(packetName);
      if (handler === undefined) {
        this.options.dispatchErrors.report({
          surface: ZLinkDispatchErrorSurface.Channel,
          messageKind: ZLinkDispatchMessageKind.Send,
          reason: ZLinkDispatchErrorReason.HandlerMissing,
          action: ZLinkDispatchErrorAction.Drop,
          packetName,
          channelName: this.options.channelName,
          correlationId: envelope.header.correlationId ?? undefined
        });
        return;
      }
      const context: ZLinkSendContext = {
        channelName: this.options.channelName,
        contentType: envelope.header.contentType,
        packetName
      };
      try {
        await invokeZLinkHandlerFilters(
          this.filters,
          { context, handler },
          () => Promise.resolve(handler.handle(decodeChannelPayload(envelope, this.options.codecs), context))
        );
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

    const context: ZLinkHandlerContext = {
      channelName: this.options.channelName,
      contentType: envelope.header.contentType,
      packetName
    };
    try {
      const reply = await invokeZLinkHandlerFilters(
        this.filters,
        { context, handler },
        () => Promise.resolve(handler.handle(decodeChannelPayload(envelope, this.options.codecs), context))
      );
      appendParts(
        router.reply(received.routingId, received.requestSeq),
        encodeChannelReplyParts(envelope.header, reply, this.options.codecs)
      ).submit();
    } catch (error) {
      appendParts(
        router.reply(received.routingId, received.requestSeq),
        encodeChannelErrorReplyParts(envelope.header, error instanceof Error ? error.message : String(error))
      ).submit();
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
    private readonly router: ZLinkBackendRouterSocket & {
      reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
    },
    private readonly dispatcher: ZLinkChannelRequestDispatcher
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

  private async dispatchAndClose(received: { parts: readonly Message[]; routingId: unknown; requestSeq: bigint | null; close(): void }): Promise<void> {
    try {
      await this.dispatcher.dispatch(received, this.router);
    } finally {
      received.close();
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
    const handler = this.options.handlers.get(packetName);
    if (handler === undefined) {
      this.options.dispatchErrors.report({
        surface: ZLinkDispatchErrorSurface.Channel,
        messageKind: ZLinkDispatchMessageKind.Publish,
        reason: ZLinkDispatchErrorReason.HandlerMissing,
        action: ZLinkDispatchErrorAction.Drop,
        packetName,
        channelName: this.options.channelName,
        topic: envelope.header.topic ?? topicMessage.topic,
        sourceRid: envelope.header.source ?? undefined,
        correlationId: envelope.header.correlationId ?? undefined
      });
      return;
    }

    const context: ZLinkPublishContext = {
      channelName: this.options.channelName,
      packetName,
      contentType: envelope.header.contentType,
      topic: envelope.header.topic ?? topicMessage.topic,
      source: envelope.header.source ?? undefined
    };
    try {
      await invokeZLinkHandlerFilters(
        this.filters,
        { context, handler },
        () => Promise.resolve(handler.handle(decodeChannelPayload(envelope, this.options.codecs), context))
      );
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
  readonly spotRouteNode?: ZLinkBackendSpotNode;
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

  constructor(options: ZLinkRoutePacketDispatcherOptions) {
    this.routerChannelId = options.routerChannelId;
    this.codecs = options.codecs;
    this.dispatchErrors = options.dispatchErrors;
    this.spotRouteNode = options.spotRouteNode;
    for (const handler of options.handlers) {
      const target = handler.kind === 'send' ? this.sendHandlers : this.requestHandlers;
      if (target.has(handler.packetName)) {
        throw new ZLinkConfigurationException(`Duplicate routed handler '${options.routerChannelId}:${handler.kind}:${handler.packetName}'.`);
      }
      target.set(handler.packetName, handler.handler as never);
    }
  }

  private readonly routerChannelId: string;
  private readonly spotRouteNode?: ZLinkBackendSpotNode;

  async dispatch(received: {
    parts: readonly Message[];
    routingId: unknown;
    spotRid?: unknown;
    requestSeq: bigint | null;
    send?: () => ZLinkMultipartOperation<ZLinkMultipartSubmitOperation>;
  }, router: {
    reply(routingId: unknown, requestSeq: bigint): ZLinkMultipartReplyOperation;
  }): Promise<void> {
    if (received.spotRid !== null && received.spotRid !== undefined) {
      if (received.send === undefined) {
        throw new ZLinkConfigurationException('Routed SPOT packet is missing a local SPOT delivery context.');
      }
      appendParts(received.send(), received.parts).submit();
      return;
    }
    if (this.spotRouteNode !== undefined) {
      const processed = this.spotRouteNode.tryProcessExternalRouterParts(received.parts);
      if (processed) {
        return;
      }
    }
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
        this.dispatchErrors.report({
          surface: ZLinkDispatchErrorSurface.RouteMeshChannel,
          messageKind: ZLinkDispatchMessageKind.Send,
          reason: ZLinkDispatchErrorReason.HandlerMissing,
          action: ZLinkDispatchErrorAction.Drop,
          packetName,
          channelName: this.routerChannelId,
          sourceRid: String(received.routingId),
          correlationId: envelope.header.correlationId ?? undefined
        });
        return;
      }
      try {
        await handler.handle(decodeChannelPayload(envelope, this.codecs), this.createRouteContext(packetName, received.routingId));
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
        sourceRid: String(received.routingId),
        correlationId: envelope.header.correlationId ?? undefined
      });
      return;
    }

    try {
      const reply = await handler.handle(decodeChannelPayload(envelope, this.codecs), this.createRouteContext(packetName, received.routingId, received.requestSeq));
      appendParts(
        router.reply(received.routingId, received.requestSeq),
        encodeChannelReplyParts(envelope.header, reply, this.codecs)
      ).submit();
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

export class ZLinkSpotRouteReceiveLoop {
  private stopped = false;
  private started = false;

  constructor(
    private readonly spotNode: ZLinkBackendSpotNode
  ) {}

  async run(signal?: AbortSignal): Promise<void> {
    if (!this.started && process.env.ZLINK_DEBUG_RUNTIME_TASKS === '1') {
      this.started = true;
      console.error('[zlink-runtime-task] spot route receive loop started');
    }
    while (!this.stopped && signal?.aborted !== true) {
      this.spotNode.processExternalRouter();
      await new Promise<void>((resolve) => setImmediate(resolve));
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
      this.registration.requestTimeoutMs
    );
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
      this.registration.requestTimeoutMs
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
    private readonly defaultTimeoutMs?: number
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
    return this.submitter<TReply>(this.packet, this.timeoutMs ?? this.defaultTimeoutMs, signal);
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
  if (signal?.aborted === true) {
    throw new Error('The operation was aborted.');
  }
}
