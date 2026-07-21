import type { Message } from '@zlink-systems/zlink';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkSubmitStatus,
  type ZLinkPublishResult,
  type ZLinkSubmitResult
} from '../../contracts';
import {
  ZLinkRuntimeMessageFlowOutcome as ZLinkMessageFlowOutcome,
  ZLinkDispatchErrorSurface,
  ZLinkDispatchMessageKind
} from '../../contracts/Dispatch/ZLinkDispatchOptions';
import type { ZLinkBackendSendFlags } from '../backend/contracts';
import { throwIfAborted } from '../abort';
import {
  closeMessages,
  decodeChannelReply,
  encodeChannelEnvelopeParts,
  newChannelCorrelationId,
  type ZLinkChannelEnvelopeCodecRegistry,
  ZLinkChannelMessageKind
} from './channel-envelope';
import { ZLinkChannelDispatchServices } from './channel-dispatch-services';
import { codecsForFrameworkPacket } from './channel-framework-packets';
import { ZLinkChannelSocketRegistry } from './channel-socket-registry';

const ZLINK_SEND_DONT_WAIT = 1 as ZLinkBackendSendFlags;

export class ZLinkChannelOutboundOperations {
  constructor(
    private readonly sockets: ZLinkChannelSocketRegistry,
    private readonly codecs: ZLinkChannelEnvelopeCodecRegistry,
    private readonly dispatchServices: ZLinkChannelDispatchServices
  ) {}

  trySend(
    channelName: string,
    packetName: string | undefined,
    message: unknown,
    metadata: ReadonlyMap<string, string> = new Map()
  ): ZLinkSubmitResult {
    const dealer = this.sockets.clientDealer(channelName);
    const correlationId = newChannelCorrelationId();
    const parts = encodeChannelEnvelopeParts(
      ZLinkChannelMessageKind.Command,
      channelName,
      packetName,
      message,
      undefined,
      undefined,
      this.codecs,
      correlationId,
      this.dispatchServices.flowCreationEnabled(),
      metadata
    ) as readonly Message[];
    const accepted = this.sockets.requireSubmitter(dealer).trySubmitCommand(
      () => dealer.send(parts, ZLINK_SEND_DONT_WAIT),
      () => closeMessages(parts)
    );
    if (!accepted) {
      return { status: ZLinkSubmitStatus.Backpressured };
    }
    this.traceSend(channelName, packetName, correlationId);
    return { status: ZLinkSubmitStatus.Submitted };
  }

  async send(
    channelName: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal,
    metadata: ReadonlyMap<string, string> = new Map()
  ): Promise<ZLinkSubmitResult> {
    throwIfAborted(signal);
    const dealer = this.sockets.clientDealer(channelName);
    const correlationId = newChannelCorrelationId();
    const parts = encodeChannelEnvelopeParts(
      ZLinkChannelMessageKind.Command,
      channelName,
      packetName,
      message,
      undefined,
      undefined,
      this.codecs,
      correlationId,
      this.dispatchServices.flowCreationEnabled(),
      metadata
    ) as readonly Message[];
    try {
      await this.sockets.requireSubmitter(dealer).submitCommand(
        () => dealer.send(parts, ZLINK_SEND_DONT_WAIT),
        signal,
        () => closeMessages(parts)
      );
    } catch (error) {
      if (error instanceof Error && /timed out/i.test(error.message)) {
        return { status: ZLinkSubmitStatus.TimedOut };
      }
      throw error;
    }
    this.traceSend(channelName, packetName, correlationId);
    return { status: ZLinkSubmitStatus.Submitted };
  }

  private traceSend(
    channelName: string,
    packetName: string | undefined,
    correlationId: string
  ): void {
    this.dispatchServices.traceOutbound(ZLinkMessageFlowOutcome.Sent, () => ({
      surface: ZLinkDispatchErrorSurface.Channel,
      messageKind: ZLinkDispatchMessageKind.Send,
      channelName,
      packetName,
      correlationId
    }));
  }

  async request<TReply>(
    channelName: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<TReply> {
    throwIfAborted(signal);
    const dealer = this.sockets.clientDealer(channelName);
    const correlationId = newChannelCorrelationId();
    const parts = encodeChannelEnvelopeParts(
      ZLinkChannelMessageKind.Request,
      channelName,
      packetName,
      request,
      timeoutMs,
      undefined,
      this.codecs,
      correlationId,
      this.dispatchServices.flowCreationEnabled(),
      metadata
    ) as readonly Message[];
    this.dispatchServices.traceOutbound(ZLinkMessageFlowOutcome.Sent, () => ({
      surface: ZLinkDispatchErrorSurface.Channel,
      messageKind: ZLinkDispatchMessageKind.Request,
      channelName,
      packetName,
      correlationId
    }));
    return this.measureRequest(channelName, () => this.sockets.requireSubmitter(dealer).submitRequest(
      (resolve, reject) => {
        try {
          const submitted = dealer.request(
            parts,
            (result, replyParts) => {
              try {
                if (result !== 0) {
                  reject(new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RouteNotConnected,
                    `Channel '${channelName}' request failed with result ${result}.`,
                    true
                  ));
                  return;
                }
                const reply = decodeChannelReply<TReply>(replyParts as readonly Message[], this.codecs);
                this.dispatchServices.traceOutbound(ZLinkMessageFlowOutcome.ReplyReceived, () => ({
                  surface: ZLinkDispatchErrorSurface.Channel,
                  messageKind: ZLinkDispatchMessageKind.Request,
                  channelName,
                  packetName,
                  correlationId
                }));
                resolve(reply);
              } catch (error) {
                reject(error);
              } finally {
                closeMessages(replyParts as readonly Message[]);
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
      timeoutMs,
      () => closeMessages(parts)
    ));
  }

  tryPublish(
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: unknown,
    metadata: ReadonlyMap<string, string> = new Map()
  ): ZLinkPublishResult {
    const publisher = this.sockets['publisher'](channelName);
    const correlationId = newChannelCorrelationId();
    const parts = encodeChannelEnvelopeParts(
      ZLinkChannelMessageKind.Publish,
      channelName,
      packetName,
      event,
      undefined,
      topic,
      this.codecs,
      correlationId,
      this.dispatchServices.flowCreationEnabled(),
      metadata
    ) as readonly Message[];
    const accepted = this.sockets.requireSubmitter(publisher).trySubmitCommand(
      () => publisher.publish(topic, parts, ZLINK_SEND_DONT_WAIT),
      () => closeMessages(parts)
    );
    if (!accepted) {
      return publishResult(ZLinkSubmitStatus.Backpressured);
    }
    this.tracePublish(channelName, topic, packetName, correlationId);
    return publishResult(ZLinkSubmitStatus.Submitted);
  }

  async publish(
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: unknown,
    signal?: AbortSignal,
    metadata: ReadonlyMap<string, string> = new Map()
  ): Promise<ZLinkPublishResult> {
    throwIfAborted(signal);
    const publisher = this.sockets['publisher'](channelName);
    const correlationId = newChannelCorrelationId();
    const parts = encodeChannelEnvelopeParts(
      ZLinkChannelMessageKind.Publish,
      channelName,
      packetName,
      event,
      undefined,
      topic,
      this.codecs,
      correlationId,
      this.dispatchServices.flowCreationEnabled(),
      metadata
    ) as readonly Message[];
    try {
      await this.sockets.requireSubmitter(publisher).submitCommand(
        () => publisher.publish(topic, parts, ZLINK_SEND_DONT_WAIT),
        signal,
        () => closeMessages(parts)
      );
    } catch (error) {
      if (error instanceof Error && /timed out/i.test(error.message)) {
        return publishResult(ZLinkSubmitStatus.TimedOut);
      }
      throw error;
    }
    this.tracePublish(channelName, topic, packetName, correlationId);
    return publishResult(ZLinkSubmitStatus.Submitted);
  }

  private tracePublish(
    channelName: string,
    topic: string,
    packetName: string | undefined,
    correlationId: string
  ): void {
    this.dispatchServices.metrics().count('zlink.fanout.published', 1, { topic });
    this.dispatchServices.traceOutbound(ZLinkMessageFlowOutcome.Sent, () => ({
      surface: ZLinkDispatchErrorSurface.Channel,
      messageKind: ZLinkDispatchMessageKind.Publish,
      channelName,
      packetName,
      correlationId,
      topic
    }));
  }

  tryRouteSubmit(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    message: unknown,
    metadata: ReadonlyMap<string, string> = new Map()
  ): ZLinkSubmitResult {
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
      correlationId,
      this.dispatchServices.flowCreationEnabled(),
      metadata
    ) as readonly Message[];
    const accepted = this.sockets.requireSubmitter(router).trySubmitCommand(
      () => router.send(targetNodeRid, parts, ZLINK_SEND_DONT_WAIT),
      () => closeMessages(parts)
    );
    if (!accepted) {
      return { status: ZLinkSubmitStatus.Backpressured };
    }
    this.traceRouteSend(routerChannelId, targetNodeRid, packetName, correlationId);
    return { status: ZLinkSubmitStatus.Submitted };
  }

  async routeSubmit(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal,
    metadata: ReadonlyMap<string, string> = new Map()
  ): Promise<ZLinkSubmitResult> {
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
      correlationId,
      this.dispatchServices.flowCreationEnabled(),
      metadata
    ) as readonly Message[];
    try {
      await this.sockets.requireSubmitter(router).submitCommand(
        () => router.send(targetNodeRid, parts, ZLINK_SEND_DONT_WAIT),
        signal,
        () => closeMessages(parts)
      );
    } catch (error) {
      if (error instanceof Error && /timed out/i.test(error.message)) {
        return { status: ZLinkSubmitStatus.TimedOut };
      }
      throw error;
    }
    this.traceRouteSend(routerChannelId, targetNodeRid, packetName, correlationId);
    return { status: ZLinkSubmitStatus.Submitted };
  }

  private traceRouteSend(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    correlationId: string
  ): void {
    this.dispatchServices.traceOutbound(ZLinkMessageFlowOutcome.Sent, () => ({
      surface: ZLinkDispatchErrorSurface.RouteMeshChannel,
      messageKind: ZLinkDispatchMessageKind.Send,
      channelName: routerChannelId,
      packetName,
      correlationId,
      sourceRid: targetNodeRid
    }));
  }

  async routeRequest<TReply>(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal,
    metadata?: ReadonlyMap<string, string>
  ): Promise<TReply> {
    throwIfAborted(signal);
    const router = this.sockets.routeRouter(routerChannelId);
    if (this.sockets.routeMemberStatus(routerChannelId, targetNodeRid) === 'missing') {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.RequestTargetNotFound,
        `Route channel '${routerChannelId}' has no member '${targetNodeRid}'.`
      );
    }
    const correlationId = newChannelCorrelationId();
    const parts = encodeChannelEnvelopeParts(
      ZLinkChannelMessageKind.Request,
      routerChannelId,
      packetName,
      request,
      timeoutMs,
      undefined,
      codecsForFrameworkPacket(packetName, this.codecs),
      correlationId,
      this.dispatchServices.flowCreationEnabled(),
      metadata
    ) as readonly Message[];
    this.dispatchServices.traceOutbound(ZLinkMessageFlowOutcome.Sent, () => ({
      surface: ZLinkDispatchErrorSurface.RouteMeshChannel,
      messageKind: ZLinkDispatchMessageKind.Request,
      channelName: routerChannelId,
      packetName,
      correlationId,
      sourceRid: targetNodeRid
    }));
    try {
      return await this.measureRequest(routerChannelId, () => this.sockets.requireSubmitter(router).submitRequest(
      (resolve, reject) => {
        let submitted: boolean;
        try {
          submitted = router.request(
            targetNodeRid,
            parts,
            (result, replyParts) => {
              try {
                if (result !== 0) {
                  reject(new ZLinkFrameworkException(
                    ZLinkFrameworkErrorKind.RouteNotConnected,
                    `Route channel '${routerChannelId}' request failed with result ${result}.`,
                    true
                  ));
                  return;
                }
                const reply = decodeChannelReply<TReply>(replyParts as readonly Message[], this.codecs);
                this.dispatchServices.traceOutbound(ZLinkMessageFlowOutcome.ReplyReceived, () => ({
                  surface: ZLinkDispatchErrorSurface.RouteMeshChannel,
                  messageKind: ZLinkDispatchMessageKind.Request,
                  channelName: routerChannelId,
                  packetName,
                  correlationId,
                  sourceRid: targetNodeRid
                }));
                resolve(reply);
              } catch (error) {
                reject(error);
              } finally {
                closeMessages(replyParts as readonly Message[]);
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
          reject(new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.RouteNotConnected,
            `Route channel '${routerChannelId}' is not ready for request.`,
            true
          ));
        }
        return submitted;
      },
      signal,
      timeoutMs,
      () => closeMessages(parts)
      ));
    } catch (error) {
      if (
        this.sockets.routeMemberStatus(routerChannelId, targetNodeRid) === 'disconnected'
        && error instanceof Error
        && (/timed out/i.test(error.message) || error.name === 'ZLinkRouteDisconnectedError')
      ) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.RouteNotConnected,
          `Route channel '${routerChannelId}' member '${targetNodeRid}' is not connected.`,
          true,
          error
        );
      }
      throw error;
    }
  }

  private async measureRequest<T>(channel: string, operation: () => Promise<T>): Promise<T> {
    const metrics = this.dispatchServices.metrics();
    if (!metrics.enabled()) return operation();
    const started = process.hrtime.bigint();
    metrics.change('zlink.channel.request.inflight', 1, { channel });
    try {
      return await operation();
    } catch (error) {
      if (error instanceof Error && /timed out/i.test(error.message)) {
        metrics.count('zlink.channel.request.timeouts', 1, { channel });
      }
      throw error;
    } finally {
      metrics.change('zlink.channel.request.inflight', -1, { channel });
      metrics.duration('zlink.channel.request.duration', Number(process.hrtime.bigint() - started) / 1e9, { channel });
    }
  }
}

function publishResult(status: ZLinkSubmitStatus): ZLinkPublishResult {
  return {
    status,
    detail: {
      snapshotRemoteNodeCount: 0n,
      admittedRemoteNodeCount: 0n,
      droppedRemoteNodeCount: 0n,
      unreachableRemoteNodeCount: 0n,
      snapshotLocalSpotCount: 0n,
      admittedLocalSpotCount: 0n,
      droppedLocalSpotCount: 0n
    }
  };
}
