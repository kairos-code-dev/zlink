import type { Message } from '@zlink-systems/zlink';
import type { RoutingId } from '../../contracts';
import { ZLinkConfigurationException, type ZLinkFrameworkRegistration } from '../configuration';
import type {
  ZLinkBackendSpot,
  ZLinkBackendSpotNode,
  ZLinkBackendSpotRouteBridge
} from '../backend/contracts';
import type { ZLinkSpotRouteTarget } from '../spots/spot-routing-internal';
import {
  closeMessages,
  decodeChannelReply,
  encodeChannelEnvelopeParts,
  type ZLinkChannelEnvelopeCodecRegistry,
  ZLinkChannelMessageKind
} from './channel-envelope';
import { codecsForFrameworkPacket } from './channel-framework-packets';
import { appendParts } from './channel-multipart';
import { throwIfAborted } from '../abort';
import {
  delay,
  isTransientRouteNotReadyError
} from './route-readiness';
import type { ZLinkChannelSocketRegistry } from './channel-socket-registry';
import type { ZLinkSpotRouteBridgeRawReplyRegistry } from './spot-route-bridge-raw-reply';
import { ZLinkSourceSpotRouter } from './source-spot-router';
import { ZLinkSpotRouteTargetResolver } from './spot-route-target-resolver';
import { ZLinkSpotNodeRouteTransport } from './spot-node-route-transport';

const ZLINK_SEND_DONT_WAIT = 1;

export interface ZLinkLocalSpotRouteDispatcher {
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
}

export interface ZLinkSpotRouteDispatchStrategyOptions {
  readonly registration: ZLinkFrameworkRegistration;
  readonly sockets: ZLinkChannelSocketRegistry;
  readonly codecs: ZLinkChannelEnvelopeCodecRegistry;
  readonly spotRouteBridges: ReadonlyMap<string, ZLinkBackendSpotRouteBridge>;
  readonly rawReplies: ZLinkSpotRouteBridgeRawReplyRegistry;
  readonly localSpotRouteDispatcher?: ZLinkLocalSpotRouteDispatcher;
}

export class ZLinkSpotRouteDispatchStrategy {
  private readonly sourceSpotRouter = new ZLinkSourceSpotRouter();
  private readonly targets: ZLinkSpotRouteTargetResolver;
  private readonly spotNodeTransport: ZLinkSpotNodeRouteTransport;

  constructor(private readonly options: ZLinkSpotRouteDispatchStrategyOptions) {
    this.targets = new ZLinkSpotRouteTargetResolver(options.registration);
    this.spotNodeTransport = new ZLinkSpotNodeRouteTransport(options.registration, this.targets);
  }

  setSpotNodes(spotNodes: ReadonlyMap<string, ZLinkBackendSpotNode>): void {
    this.targets.setSpotNodes(spotNodes);
  }

  spotRouteNode(routerChannelId: string): ZLinkBackendSpotNode | undefined {
    return this.targets.routeNode(routerChannelId);
  }

  canRouteChannel(routerChannelId: string): boolean {
    return this.targets.canRouteChannel(routerChannelId);
  }

  canRoutePacketChannel(routerChannelId: string): boolean {
    return this.targets.canRoutePacketChannel(routerChannelId);
  }

  async routeSendToSpot(
    spotRouteTarget: ZLinkSpotRouteTarget,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    const parts = encodeChannelEnvelopeParts(
      ZLinkChannelMessageKind.Command,
      spotRouteTarget.routerChannelId,
      packetName,
      message,
      undefined,
      undefined,
      codecsForFrameworkPacket(packetName, this.options.codecs)
    ) as readonly Message[];
    const localSpotRouteNode = this.targets.localRouteNode(spotRouteTarget);
    if (localSpotRouteNode !== undefined) {
      try {
        const localDispatcher = this.options.localSpotRouteDispatcher;
        if (localDispatcher === undefined) {
          throw new ZLinkConfigurationException(`Route channel '${spotRouteTarget.routerChannelId}' could not dispatch local SPOT send.`);
        }
        await localDispatcher.send(spotRouteTarget.spotRid, packetName, message, {
          channelName: spotRouteTarget.routerChannelId,
          signal
        });
        return;
      } finally {
        closeMessages(parts);
      }
    }
    const bridge = this.options.spotRouteBridges.get(spotRouteTarget.routerChannelId);
    if (bridge !== undefined) {
      try {
        const submitter = this.options.sockets.requireSubmitter(this.options.sockets.routeRouter(spotRouteTarget.routerChannelId));
        const timeoutMs = this.options.registration.requestTimeoutMs ?? 30_000;
        const deadline = Date.now() + timeoutMs;
        for (;;) {
          try {
            await submitter.submitCommand(
              () => {
                const submitted = appendParts(
                  bridge.send(spotRouteTarget.routerChannelId, spotRouteTarget.targetNodeRid, spotRouteTarget.spotRid),
                  parts
                ).flags(ZLINK_SEND_DONT_WAIT).submit();
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
    if (this.targets.hasBoundRouteRouter(spotRouteTarget.routerChannelId)) {
      const router = this.options.sockets.routeRouter(spotRouteTarget.routerChannelId);
      await this.options.sockets.requireSubmitter(router).submitCommand(
        () => router.sendToSpot(
          spotRouteTarget.targetNodeRid,
          spotRouteTarget.spotRid,
          parts,
          ZLINK_SEND_DONT_WAIT
        ),
        signal
      );
      return;
    }
    if (await this.spotNodeTransport.send(spotRouteTarget, parts, signal)) {
      return;
    }
    const router = this.options.sockets.routeRouter(spotRouteTarget.routerChannelId);
    await this.options.sockets.requireSubmitter(router).submitCommand(
      () => router.sendToSpot(
        spotRouteTarget.targetNodeRid,
        spotRouteTarget.spotRid,
        parts,
        ZLINK_SEND_DONT_WAIT
      ),
      signal
    );
  }

  async routeRequestToSpot<TReply>(
    spotRouteTarget: ZLinkSpotRouteTarget,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> {
    throwIfAborted(signal);
    const codecs = codecsForFrameworkPacket(packetName, this.options.codecs);
    const parts = encodeChannelEnvelopeParts(ZLinkChannelMessageKind.Request, spotRouteTarget.routerChannelId, packetName, request, timeoutMs, undefined, codecs) as readonly Message[];
    const localSpotRouteNode = this.targets.localRouteNode(spotRouteTarget);
    if (localSpotRouteNode !== undefined) {
      try {
        const localDispatcher = this.options.localSpotRouteDispatcher;
        if (localDispatcher === undefined) {
          throw new ZLinkConfigurationException(`Route channel '${spotRouteTarget.routerChannelId}' could not dispatch local SPOT request.`);
        }
        return await this.withLocalSpotRouteRequestTimeout(
          spotRouteTarget.routerChannelId,
          localDispatcher.request<TReply>(spotRouteTarget.spotRid, packetName, request, {
            channelName: spotRouteTarget.routerChannelId,
            signal
          }),
          timeoutMs
        );
      } finally {
        closeMessages(parts);
      }
    }
    const bridge = this.options.spotRouteBridges.get(spotRouteTarget.routerChannelId);
    if (bridge !== undefined) {
      return this.options.sockets.requireSubmitter(this.options.sockets.routeRouter(spotRouteTarget.routerChannelId)).submitRequest(
        (resolve, reject) => {
          const submitted = appendParts(
            bridge.request(spotRouteTarget.routerChannelId, spotRouteTarget.targetNodeRid, spotRouteTarget.spotRid),
            parts
          )
            .timeout(timeoutMs ?? 0)
            .flags(ZLINK_SEND_DONT_WAIT)
            .submit((result, replyParts) => {
              try {
                if (result !== 0) {
                  reject(new ZLinkConfigurationException(`Route channel '${spotRouteTarget.routerChannelId}' spot request failed with result ${result}.`));
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
              reject(new ZLinkConfigurationException(`Route channel '${spotRouteTarget.routerChannelId}' is not ready for SPOT request.`));
            }
          }
          return submitted;
        },
        signal,
        timeoutMs
      );
    }
    if (this.targets.hasBoundRouteRouter(spotRouteTarget.routerChannelId)) {
      const router = this.options.sockets.routeRouter(spotRouteTarget.routerChannelId);
      return this.options.sockets.requireSubmitter(router).submitRequest(
        (resolve, reject) => {
          const submitted = router.requestToSpot(
            spotRouteTarget.targetNodeRid,
            spotRouteTarget.spotRid,
            parts,
            (result, parts) => {
              try {
                if (result !== 0) {
                  reject(new ZLinkConfigurationException(`Route channel '${spotRouteTarget.routerChannelId}' spot request failed with result ${result}.`));
                  return;
                }
                resolve(decodeChannelReply<TReply>(parts as readonly Message[], codecs));
              } catch (error) {
                reject(error);
              } finally {
                closeMessages(parts as readonly Message[]);
              }
            },
            ZLINK_SEND_DONT_WAIT,
            timeoutMs
          );
          if (!submitted) {
            try {
              closeMessages(parts);
            } finally {
              reject(new ZLinkConfigurationException(`Route channel '${spotRouteTarget.routerChannelId}' is not ready for SPOT request.`));
            }
          }
          return submitted;
        },
        signal,
        timeoutMs
      );
    }
    const spotNodeRequest = this.spotNodeTransport.request<TReply>(
      spotRouteTarget,
      parts,
      codecs,
      timeoutMs,
      signal
    );
    if (spotNodeRequest !== undefined) {
      return spotNodeRequest;
    }
    const router = this.options.sockets.routeRouter(spotRouteTarget.routerChannelId);
    return this.options.sockets.requireSubmitter(router).submitRequest(
      (resolve, reject) => {
        const submitted = router.requestToSpot(
          spotRouteTarget.targetNodeRid,
          spotRouteTarget.spotRid,
          parts,
          (result, parts) => {
            try {
              if (result !== 0) {
                reject(new ZLinkConfigurationException(`Route channel '${spotRouteTarget.routerChannelId}' spot request failed with result ${result}.`));
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
            reject(new ZLinkConfigurationException(`Route channel '${spotRouteTarget.routerChannelId}' is not ready for SPOT request.`));
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
    spotRouteTarget: ZLinkSpotRouteTarget,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> {
    return await this.sourceSpotRouter.request<TReply>(
      sourceSpot,
      spotRouteTarget,
      packetName,
      request,
      timeoutMs,
      signal
    );
  }

  async routeSendFromSpotToSpot(
    sourceSpot: ZLinkBackendSpot,
    spotRouteTarget: ZLinkSpotRouteTarget,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): Promise<void> {
    await this.sourceSpotRouter.send(sourceSpot, spotRouteTarget, packetName, message, signal);
  }

  async routeRequestRawFromSpotToSpot(
    sourceSpot: ZLinkBackendSpot,
    spotRouteTarget: ZLinkSpotRouteTarget,
    request: Message,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<readonly Message[]> {
    return await this.sourceSpotRouter.requestRaw(sourceSpot, spotRouteTarget, request, timeoutMs, signal);
  }

  async routeRequestRawToSpot(
    spotRouteTarget: ZLinkSpotRouteTarget,
    request: Message,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<readonly Message[]> {
    throwIfAborted(signal);
    const bridge = this.options.spotRouteBridges.get(spotRouteTarget.routerChannelId);
    if (bridge !== undefined) {
      const effectiveTimeoutMs = timeoutMs ?? this.options.registration.requestTimeoutMs;
      return new Promise<readonly Message[]>((resolve, reject) => {
        const pending = this.options.rawReplies.enqueue(
          spotRouteTarget.routerChannelId,
          resolve,
          reject,
          timeoutMs,
          this.options.registration.requestTimeoutMs,
          signal
        );
        try {
          const submission = this.options.sockets
            .requireSubmitter(this.options.sockets.routeRouter(spotRouteTarget.routerChannelId))
            .submitRequest<void>(
              (complete, fail) => {
                if (!pending.attachSubmission(complete, fail)) return true;
                const submitted = bridge.request(spotRouteTarget.routerChannelId, spotRouteTarget.targetNodeRid, spotRouteTarget.spotRid)
                  .message(request)
                  .timeout(effectiveTimeoutMs ?? 0)
                  .submit((result, replyParts) => {
                    if (result !== 0) {
                      closeMessages(replyParts as readonly Message[]);
                      const error = new ZLinkConfigurationException(`Route channel '${spotRouteTarget.routerChannelId}' spot request failed with result ${result}.`);
                      pending.reject(error, true);
                      return;
                    }
                    pending.resolve(replyParts as readonly Message[]);
                  });
                return submitted;
              },
              undefined,
              -1
            );
          void submission.catch((error) => pending.reject(error, true));
        } catch (error) {
          pending.reject(error, true);
        }
      });
    }
    const spotNodeRequest = this.spotNodeTransport.requestRaw(spotRouteTarget, request, timeoutMs, signal);
    if (spotNodeRequest !== undefined) {
      return spotNodeRequest;
    }

    const router = this.options.sockets.routeRouter(spotRouteTarget.routerChannelId);
    return this.options.sockets.requireSubmitter(router).submitRequest(
      (resolve, reject) => {
        const submitted = router.requestToSpot(
          spotRouteTarget.targetNodeRid,
          spotRouteTarget.spotRid,
          [request],
          (result, replyParts) => {
            if (result !== 0) {
              closeMessages(replyParts as readonly Message[]);
              reject(new ZLinkConfigurationException(`Route channel '${spotRouteTarget.routerChannelId}' spot request failed with result ${result}.`));
              return;
            }
            resolve(replyParts as readonly Message[]);
          },
          0,
          timeoutMs
        );
        if (!submitted) {
          reject(new ZLinkConfigurationException(`Route channel '${spotRouteTarget.routerChannelId}' is not ready for SPOT request.`));
        }
        return submitted;
      },
      signal,
      timeoutMs
    );
  }

  private withLocalSpotRouteRequestTimeout<T>(
    routerChannelId: string,
    request: Promise<T>,
    timeoutMs: number | undefined
  ): Promise<T> {
    const effectiveTimeoutMs = timeoutMs ?? this.options.registration.requestTimeoutMs;
    if (effectiveTimeoutMs === undefined) {
      return request;
    }
    return new Promise<T>((resolve, reject) => {
      const timeout = setTimeout(
        () => reject(new ZLinkConfigurationException(`Route channel '${routerChannelId}' local SPOT request timed out.`)),
        effectiveTimeoutMs
      );
      request.then(
        (value) => {
          clearTimeout(timeout);
          resolve(value);
        },
        (error) => {
          clearTimeout(timeout);
          reject(error);
        }
      );
    });
  }
}
