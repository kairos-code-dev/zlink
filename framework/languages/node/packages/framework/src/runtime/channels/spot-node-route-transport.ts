import type { Message } from '@zlink-systems/zlink';
import type { ZLinkFrameworkRegistration } from '../configuration';
import { ZLinkConfigurationException } from '../configuration';
import { createAbortError, throwIfAborted } from '../abort';
import type { ZLinkSpotRouteTarget } from '../spots/spot-routing-internal';
import {
  closeMessages,
  decodeChannelReply,
  type ZLinkChannelEnvelopeCodecRegistry
} from './channel-envelope';
import { ZLinkSpotRouteTargetResolver } from './spot-route-target-resolver';

export class ZLinkSpotNodeRouteTransport {
  private readonly routerQueues = new Map<string, Promise<void>>();

  constructor(
    private readonly registration: ZLinkFrameworkRegistration,
    private readonly targets: ZLinkSpotRouteTargetResolver
  ) {}

  async send(
    target: ZLinkSpotRouteTarget,
    parts: readonly Message[],
    signal?: AbortSignal
  ): Promise<boolean> {
    const router = this.targets.spotNodeRouter(target.routerChannelId);
    if (router === undefined) {
      return false;
    }
    try {
      await this.enqueue(target.routerChannelId, () => {
        throwIfAborted(signal);
        if (!router.sendToSpot(target.targetNodeRid, target.spotId, parts, 0)) {
          throw this.notReady(target.routerChannelId, 'send');
        }
      });
      return true;
    } finally {
      closeMessages(parts);
    }
  }

  request<TReply>(
    target: ZLinkSpotRouteTarget,
    parts: readonly Message[],
    codecs: ZLinkChannelEnvelopeCodecRegistry | undefined,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> | undefined {
    const router = this.targets.spotNodeRouter(target.routerChannelId);
    if (router === undefined) {
      return undefined;
    }
    const effectiveTimeoutMs = timeoutMs ?? this.registration.requestTimeoutMs ?? 30_000;
    return this.enqueuePhysicalRequest(target.routerChannelId, (releasePhysical) => {
      let physicalSubmitted = false;
      const result = this.submitRequest<TReply>(
        effectiveTimeoutMs,
        (resolve, reject) => {
          const submitted = router.requestToSpot(
            target.targetNodeRid,
            target.spotId,
            parts,
            (result, replyParts) => {
              releasePhysical();
              try {
                if (result !== 0) {
                  reject(this.requestFailure(target.routerChannelId, result));
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
            effectiveTimeoutMs
          );
          physicalSubmitted = submitted;
          return submitted;
        },
        this.notReady(target.routerChannelId, 'request'),
        signal
      ).finally(() => closeMessages(parts));
      void result.catch(() => { if (!physicalSubmitted) releasePhysical(); });
      return result;
    });
  }

  requestRaw(
    target: ZLinkSpotRouteTarget,
    request: Message,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<readonly Message[]> | undefined {
    const router = this.targets.spotNodeRouter(target.routerChannelId);
    if (router === undefined) {
      return undefined;
    }
    const effectiveTimeoutMs = timeoutMs ?? this.registration.requestTimeoutMs ?? 30_000;
    return this.enqueuePhysicalRequest(target.routerChannelId, (releasePhysical) => {
      let physicalSubmitted = false;
      const result = this.submitRequest<readonly Message[]>(
        effectiveTimeoutMs,
        (resolve, reject) => {
          const submitted = router.requestToSpot(
            target.targetNodeRid,
            target.spotId,
            request,
            (result, replyParts) => {
              releasePhysical();
              if (signal?.aborted === true) {
                closeMessages(replyParts as readonly Message[]);
                reject(createAbortError());
                return;
              }
              if (result !== 0) {
                closeMessages(replyParts as readonly Message[]);
                reject(this.requestFailure(target.routerChannelId, result));
                return;
              }
              if (!resolve(replyParts as readonly Message[])) {
                closeMessages(replyParts as readonly Message[]);
              }
            },
            0,
            effectiveTimeoutMs
          );
          physicalSubmitted = submitted;
          return submitted;
        },
        this.notReady(target.routerChannelId, 'request'),
        signal
      );
      void result.catch(() => { if (!physicalSubmitted) releasePhysical(); });
      return result;
    });
  }

  private enqueue<T>(routerChannelId: string, operation: () => Promise<T> | T): Promise<T> {
    const previous = this.routerQueues.get(routerChannelId) ?? Promise.resolve();
    let releaseCurrent: () => void = () => {};
    const current = new Promise<void>((resolve) => {
      releaseCurrent = resolve;
    });
    const queued = previous.catch(() => undefined).then(() => current);
    this.routerQueues.set(routerChannelId, queued);
    return previous
      .catch(() => undefined)
      .then(operation)
      .finally(() => {
        releaseCurrent();
        if (this.routerQueues.get(routerChannelId) === queued) {
          this.routerQueues.delete(routerChannelId);
        }
      });
  }

  private enqueuePhysicalRequest<T>(
    routerChannelId: string,
    operation: (releasePhysical: () => void) => Promise<T>
  ): Promise<T> {
    const previous = this.routerQueues.get(routerChannelId) ?? Promise.resolve();
    let releasePhysical: () => void = () => {};
    const physical = new Promise<void>((resolve) => { releasePhysical = resolve; });
    const queued = previous.catch(() => undefined).then(() => physical);
    this.routerQueues.set(routerChannelId, queued);
    return previous
      .catch(() => undefined)
      .then(() => {
        try {
          // A submitted request owns the physical route slot until its native
          // callback arrives.  Abort/timeout only settles the caller's
          // promise; releasing here would let the next request reuse the slot
          // while the late reply still belongs to the previous request.
          return Promise.resolve(operation(releasePhysical));
        } catch (error) {
          releasePhysical();
          throw error;
        }
      })
      .finally(() => {
        void queued.finally(() => {
          if (this.routerQueues.get(routerChannelId) === queued) {
            this.routerQueues.delete(routerChannelId);
          }
        });
      });
  }

  private submitRequest<T>(
    timeoutMs: number | undefined,
    submit: (resolve: (reply: T) => boolean, reject: (error: unknown) => void) => boolean,
    notReadyError: ZLinkConfigurationException,
    signal?: AbortSignal
  ): Promise<T> {
    throwIfAborted(signal);
    const effectiveTimeoutMs = timeoutMs ?? this.registration.requestTimeoutMs ?? 30_000;
    const deadline = Date.now() + effectiveTimeoutMs;
    return new Promise<T>((resolve, reject) => {
      let retryTimer: ReturnType<typeof setTimeout> | undefined;
      const deadlineTimer = setTimeout(() => fail(notReadyError), effectiveTimeoutMs);
      let settled = false;
      const cleanup = () => {
        clearTimeout(deadlineTimer);
        if (retryTimer !== undefined) {
          clearTimeout(retryTimer);
        }
        signal?.removeEventListener('abort', onAbort);
      };
      const complete = (value: T): boolean => {
        if (settled) return false;
        settled = true;
        cleanup();
        resolve(value);
        return true;
      };
      const fail = (error: unknown) => {
        if (settled) return;
        settled = true;
        cleanup();
        reject(error);
      };
      const onAbort = () => fail(createAbortError());
      const attempt = () => {
        if (settled) return;
        try {
          if (submit(complete, fail)) {
            return;
          }
        } catch (error) {
          fail(error);
          return;
        }
        if (Date.now() >= deadline) {
          fail(notReadyError);
          return;
        }
        retryTimer = setTimeout(attempt, 10);
      };
      signal?.addEventListener('abort', onAbort, { once: true });
      attempt();
    });
  }

  private requestFailure(routerChannelId: string, result: number): ZLinkConfigurationException {
    return new ZLinkConfigurationException(
      `SpotNode router '${routerChannelId}' spot request failed with result ${result}.`
    );
  }

  private notReady(routerChannelId: string, operation: 'request' | 'send'): ZLinkConfigurationException {
    return new ZLinkConfigurationException(
      `SpotNode router '${routerChannelId}' is not ready for SPOT ${operation}.`
    );
  }
}
