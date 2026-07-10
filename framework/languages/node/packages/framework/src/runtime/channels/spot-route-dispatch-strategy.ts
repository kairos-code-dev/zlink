import type { Message } from '@zlink-systems/zlink';
import { RoutingId as BindingRoutingId } from '@zlink-systems/zlink';
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
import { throwIfAborted } from './channel-abort';
import {
  decodeSpotDirectReply,
  encodeSpotDirectEnvelope
} from './spot-direct-envelope';
import {
  delay,
  isTransientRouteNotReadyError
} from './route-readiness';
import type { ZLinkChannelSocketRegistry } from './channel-socket-registry';
import type { ZLinkSpotRouteBridgeRawReplyRegistry } from './spot-route-bridge-raw-reply';

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
  private readonly spotNodeRouterQueues = new Map<string, Promise<void>>();
  private spotNodes?: ReadonlyMap<string, ZLinkBackendSpotNode>;

  constructor(private readonly options: ZLinkSpotRouteDispatchStrategyOptions) {}

  setSpotNodes(spotNodes: ReadonlyMap<string, ZLinkBackendSpotNode>): void {
    this.spotNodes = spotNodes;
  }

  spotRouteNode(routerChannelId: string): ZLinkBackendSpotNode | undefined {
    const named = this.options.registration.spotNodes.get(routerChannelId);
    if (named?.router !== undefined) {
      return this.spotNodes?.get(routerChannelId);
    }
    const routeChannel = this.options.registration.routeChannelOptions.get(routerChannelId);
    if (routeChannel === undefined) {
      return undefined;
    }
    if (routeChannel.routingId !== undefined) {
      for (const [spotNodeName, spotNode] of this.options.registration.spotNodes.entries()) {
        if (spotNode.router?.routingId === routeChannel.routingId) {
          return this.spotNodes?.get(spotNodeName);
        }
      }
    }
    const routerNodeNames = [...this.options.registration.spotNodes.entries()]
      .filter(([, spotNode]) => spotNode.router !== undefined)
      .map(([spotNodeName]) => spotNodeName);
    if (routerNodeNames.length === 1) {
      return this.spotNodes?.get(routerNodeNames[0]);
    }
    return undefined;
  }

  canRouteChannel(routerChannelId: string): boolean {
    return this.options.registration.routeChannels.has(routerChannelId)
      || this.spotNodeRouter(routerChannelId) !== undefined;
  }

  canRoutePacketChannel(routerChannelId: string): boolean {
    if (this.spotNodes?.has(routerChannelId) ?? false) {
      return false;
    }
    return this.options.registration.routeChannels.has(routerChannelId);
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
    const localSpotRouteNode = this.localSpotRouteNode(spotRouteTarget);
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
    if (this.hasBoundRouteRouter(spotRouteTarget.routerChannelId)) {
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
    const spotNodeRouter = this.spotNodeRouter(spotRouteTarget.routerChannelId);
    if (spotNodeRouter !== undefined) {
      try {
        await this.enqueueSpotNodeRouterOperation(spotRouteTarget.routerChannelId, () => {
          if (!spotNodeRouter.sendToSpot(
            spotRouteTarget.targetNodeRid,
            spotRouteTarget.spotRid,
            parts,
            0
          )) {
            throw new ZLinkConfigurationException(`SpotNode router '${spotRouteTarget.routerChannelId}' is not ready for SPOT send.`);
          }
        });
        return;
      } finally {
        closeMessages(parts);
      }
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
    const localSpotRouteNode = this.localSpotRouteNode(spotRouteTarget);
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
    if (this.hasBoundRouteRouter(spotRouteTarget.routerChannelId)) {
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
    const spotNodeRouter = this.spotNodeRouter(spotRouteTarget.routerChannelId);
    if (spotNodeRouter !== undefined) {
      return this.enqueueSpotNodeRouterOperation(spotRouteTarget.routerChannelId, () =>
        this.submitSpotNodeRouterRequest<TReply>(
          spotRouteTarget.routerChannelId,
          timeoutMs,
          (resolve, reject) => spotNodeRouter.requestToSpot(
            spotRouteTarget.targetNodeRid,
            spotRouteTarget.spotRid,
            parts,
            (result, replyParts) => {
              try {
                if (result !== 0) {
                  reject(new ZLinkConfigurationException(`SpotNode router '${spotRouteTarget.routerChannelId}' spot request failed with result ${result}.`));
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
          `SpotNode router '${spotRouteTarget.routerChannelId}' is not ready for SPOT request.`
        ).finally(() => closeMessages(parts))
      );
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
    throwIfAborted(signal);
    const parts = [encodeSpotDirectEnvelope(ZLinkChannelMessageKind.Request, spotRouteTarget.routerChannelId, packetName, request)] as readonly Message[];
    return new Promise<TReply>((resolve, reject) => {
      try {
        if (!sourceSpot.requestToSpot(
          spotRouteTarget.targetNodeRid,
          spotRouteTarget.spotRid,
          parts,
          (result, replyParts) => {
            try {
              if (result !== 0) {
                reject(new ZLinkConfigurationException(`SpotNode router '${spotRouteTarget.routerChannelId}' spot request failed with result ${result}.`));
                return;
              }
              resolve(decodeSpotDirectReply<TReply>(replyParts as readonly Message[]));
            } catch (error) {
              reject(error);
            } finally {
              closeMessages(replyParts as readonly Message[]);
            }
          },
          0,
          timeoutMs
        )) {
          reject(new ZLinkConfigurationException(`SpotNode router '${spotRouteTarget.routerChannelId}' is not ready for SPOT request.`));
        }
      } catch (error) {
        reject(error);
      }
    }).finally(() => closeMessages(parts));
  }

  async routeSendFromSpotToSpot(
    sourceSpot: ZLinkBackendSpot,
    spotRouteTarget: ZLinkSpotRouteTarget,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    const parts = [encodeSpotDirectEnvelope(ZLinkChannelMessageKind.Command, spotRouteTarget.routerChannelId, packetName, message)] as readonly Message[];
    try {
      if (!sourceSpot.sendToSpot(
        spotRouteTarget.targetNodeRid,
        spotRouteTarget.spotRid,
        parts,
        0
      )) {
        throw new ZLinkConfigurationException(`SpotNode router '${spotRouteTarget.routerChannelId}' is not ready for SPOT send.`);
      }
    } finally {
      closeMessages(parts);
    }
  }

  async routeRequestRawFromSpotToSpot(
    sourceSpot: ZLinkBackendSpot,
    spotRouteTarget: ZLinkSpotRouteTarget,
    request: Message,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<readonly Message[]> {
    throwIfAborted(signal);
    return new Promise<readonly Message[]>((resolve, reject) => {
      try {
        if (!sourceSpot.requestToSpot(
          spotRouteTarget.targetNodeRid,
          spotRouteTarget.spotRid,
          request,
          (result, replyParts) => {
            if (result !== 0) {
              closeMessages(replyParts as readonly Message[]);
              reject(new ZLinkConfigurationException(`SpotNode router '${spotRouteTarget.routerChannelId}' spot request failed with result ${result}.`));
              return;
            }
            resolve(replyParts as readonly Message[]);
          },
          0,
          timeoutMs
        )) {
          reject(new ZLinkConfigurationException(`SpotNode router '${spotRouteTarget.routerChannelId}' is not ready for SPOT request.`));
        }
      } catch (error) {
        reject(error);
      }
    });
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
      return new Promise<readonly Message[]>((resolve, reject) => {
        const pending = this.options.rawReplies.enqueue(
          spotRouteTarget.routerChannelId,
          resolve,
          reject,
          timeoutMs,
          this.options.registration.requestTimeoutMs,
          signal
        );
        this.options.sockets.requireSubmitter(this.options.sockets.routeRouter(spotRouteTarget.routerChannelId)).submitCommand(
          () => {
            const submitted = bridge.request(spotRouteTarget.routerChannelId, spotRouteTarget.targetNodeRid, spotRouteTarget.spotRid)
              .message(request)
              .timeout(timeoutMs ?? 0)
              .submit((result, replyParts) => {
                if (result !== 0) {
                  closeMessages(replyParts as readonly Message[]);
                  pending.reject(new ZLinkConfigurationException(`Route channel '${spotRouteTarget.routerChannelId}' spot request failed with result ${result}.`));
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
    const spotNodeRouter = this.spotNodeRouter(spotRouteTarget.routerChannelId);
    if (spotNodeRouter !== undefined) {
      return this.enqueueSpotNodeRouterOperation(spotRouteTarget.routerChannelId, () =>
        this.submitSpotNodeRouterRequest<readonly Message[]>(
          spotRouteTarget.routerChannelId,
          timeoutMs,
          (resolve, reject) => spotNodeRouter.requestToSpot(
            spotRouteTarget.targetNodeRid,
            spotRouteTarget.spotRid,
            request,
            (result, replyParts) => {
              if (result !== 0) {
                closeMessages(replyParts as readonly Message[]);
                reject(new ZLinkConfigurationException(`SpotNode router '${spotRouteTarget.routerChannelId}' spot request failed with result ${result}.`));
                return;
              }
              resolve(replyParts as readonly Message[]);
            },
            0,
            timeoutMs
          ),
          `SpotNode router '${spotRouteTarget.routerChannelId}' is not ready for SPOT request.`
        )
      );
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

  private localSpotRouteNode(spotRouteTarget: ZLinkSpotRouteTarget): ZLinkBackendSpotNode | undefined {
    const routeNode =
      this.spotRouteNode(spotRouteTarget.routerChannelId) ??
      this.spotNodes?.get(spotRouteTarget.routerChannelId);
    if (routeNode !== undefined && routingIdsEqual(routeNode.routingId, spotRouteTarget.targetNodeRid)) {
      return routeNode;
    }
    return undefined;
  }

  private spotNodeRouter(routerChannelId: string): ZLinkBackendSpot | undefined {
    if (this.options.registration.spotNodes.get(routerChannelId)?.router !== undefined) {
      return this.spotNodes?.get(routerChannelId)?.entrySpot();
    }
    const routeBridgeNode = this.spotRouteNode(routerChannelId);
    if (routeBridgeNode !== undefined) {
      return routeBridgeNode.entrySpot();
    }
    return this.spotNodes?.get(routerChannelId)?.entrySpot();
  }

  private hasBoundRouteRouter(routerChannelId: string): boolean {
    return this.options.registration.routeChannelOptions.get(routerChannelId)?.bind !== undefined;
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
    _routerChannelId: string,
    timeoutMs: number | undefined,
    submit: (resolve: (reply: T) => void, reject: (error: unknown) => void) => boolean,
    notReadyMessage: string
  ): Promise<T> {
    const effectiveTimeoutMs = timeoutMs ?? this.options.registration.requestTimeoutMs ?? 30_000;
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

function routingIdsEqual(left: RoutingId | undefined, right: RoutingId | undefined): boolean {
  if (left === undefined || right === undefined) {
    return false;
  }
  const rightCandidates = routingIdHexCandidates(right);
  for (const leftCandidate of routingIdHexCandidates(left)) {
    if (rightCandidates.has(leftCandidate)) {
      return true;
    }
  }
  return false;
}

function routingIdHexCandidates(routingId: RoutingId): Set<string> {
  const candidates = new Set<string>();
  const toHex = (routingId as unknown as { toHex?: () => string }).toHex;
  if (typeof toHex === 'function') {
    candidates.add(toHex.call(routingId).toLowerCase());
  }
  const text = String(routingId);
  candidates.add(text.toLowerCase());
  try {
    candidates.add(BindingRoutingId.from(text).toHex().toLowerCase());
  } catch {
    // Some callers pass an already-encoded routing id string.
  }
  return candidates;
}
