import type { Message } from '@zlink-systems/zlink';
import {
  ZLinkDispatchErrorSurface,
  ZLinkDispatchMessageKind,
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkMessageFlowOutcome
} from '../../contracts';
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

  send(
    channelName: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): void {
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
      this.dispatchServices.flowCreationEnabled()
    ) as readonly Message[];
    this.sockets.requireSubmitter(dealer).submitCommandOneWay(
      () => dealer.send(parts, ZLINK_SEND_DONT_WAIT),
      signal,
      () => closeMessages(parts)
    );
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
    signal?: AbortSignal
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
      this.dispatchServices.flowCreationEnabled()
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

  publish(
    channelName: string,
    topic: string,
    packetName: string | undefined,
    event: unknown,
    signal?: AbortSignal
  ): void {
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
      this.dispatchServices.flowCreationEnabled()
    ) as readonly Message[];
    this.sockets.requireSubmitter(publisher).submitCommandOneWay(
      () => publisher.publish(topic, parts, ZLINK_SEND_DONT_WAIT),
      signal,
      () => closeMessages(parts)
    );
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

  routeSubmit(
    routerChannelId: string,
    targetNodeRid: string,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): void {
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
      this.dispatchServices.flowCreationEnabled()
    ) as readonly Message[];
    this.sockets.requireSubmitter(router).submitCommandOneWay(
      () => router.send(targetNodeRid, parts, ZLINK_SEND_DONT_WAIT),
      signal,
      () => closeMessages(parts)
    );
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
      correlationId,
      this.dispatchServices.flowCreationEnabled()
    ) as readonly Message[];
    this.dispatchServices.traceOutbound(ZLinkMessageFlowOutcome.Sent, () => ({
      surface: ZLinkDispatchErrorSurface.RouteMeshChannel,
      messageKind: ZLinkDispatchMessageKind.Request,
      channelName: routerChannelId,
      packetName,
      correlationId,
      sourceRid: targetNodeRid
    }));
    return this.measureRequest(routerChannelId, () => this.sockets.requireSubmitter(router).submitRequest(
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
