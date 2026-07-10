import type { Message } from '@zlink-systems/zlink';
import type { ZLinkBackendSpot } from '../backend/contracts';
import type { ZLinkSpotRouteTarget } from '../spots/spot-routing-internal';
import { createAbortError, throwIfAborted } from '../abort';
import { ZLinkConfigurationException } from '../configuration';
import { closeMessages, ZLinkChannelMessageKind } from './channel-envelope';
import { decodeSpotDirectReply, encodeSpotDirectEnvelope } from './spot-direct-envelope';

export class ZLinkSourceSpotRouter {
  async request<TReply>(
    sourceSpot: ZLinkBackendSpot,
    target: ZLinkSpotRouteTarget,
    packetName: string | undefined,
    request: unknown,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<TReply> {
    throwIfAborted(signal);
    const parts = [encodeSpotDirectEnvelope(
      ZLinkChannelMessageKind.Request,
      target.routerChannelId,
      packetName,
      request
    )] as readonly Message[];
    return new Promise<TReply>((resolve, reject) => {
      let settled = false;
      const cleanup = () => signal?.removeEventListener('abort', onAbort);
      const complete = (value: TReply) => {
        if (settled) return;
        settled = true;
        cleanup();
        resolve(value);
      };
      const fail = (error: unknown) => {
        if (settled) return;
        settled = true;
        cleanup();
        reject(error);
      };
      const onAbort = () => fail(createAbortError());
      signal?.addEventListener('abort', onAbort, { once: true });
      try {
        if (!sourceSpot.requestToSpot(
          target.targetNodeRid,
          target.spotRid,
          parts,
          (result, replyParts) => {
            try {
              if (result !== 0) {
                fail(this.requestFailure(target.routerChannelId, result));
                return;
              }
              complete(decodeSpotDirectReply<TReply>(replyParts as readonly Message[]));
            } catch (error) {
              fail(error);
            } finally {
              closeMessages(replyParts as readonly Message[]);
            }
          },
          0,
          timeoutMs
        )) {
          fail(this.notReady(target.routerChannelId, 'request'));
        }
      } catch (error) {
        fail(error);
      }
    }).finally(() => closeMessages(parts));
  }

  async send(
    sourceSpot: ZLinkBackendSpot,
    target: ZLinkSpotRouteTarget,
    packetName: string | undefined,
    message: unknown,
    signal?: AbortSignal
  ): Promise<void> {
    throwIfAborted(signal);
    const parts = [encodeSpotDirectEnvelope(
      ZLinkChannelMessageKind.Command,
      target.routerChannelId,
      packetName,
      message
    )] as readonly Message[];
    try {
      if (!sourceSpot.sendToSpot(target.targetNodeRid, target.spotRid, parts, 0)) {
        throw this.notReady(target.routerChannelId, 'send');
      }
    } finally {
      closeMessages(parts);
    }
  }

  async requestRaw(
    sourceSpot: ZLinkBackendSpot,
    target: ZLinkSpotRouteTarget,
    request: Message,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ): Promise<readonly Message[]> {
    throwIfAborted(signal);
    return new Promise<readonly Message[]>((resolve, reject) => {
      let settled = false;
      const cleanup = () => signal?.removeEventListener('abort', onAbort);
      const complete = (value: readonly Message[]): boolean => {
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
      signal?.addEventListener('abort', onAbort, { once: true });
      try {
        if (!sourceSpot.requestToSpot(
          target.targetNodeRid,
          target.spotRid,
          request,
          (result, replyParts) => {
            if (result !== 0) {
              closeMessages(replyParts as readonly Message[]);
              fail(this.requestFailure(target.routerChannelId, result));
              return;
            }
            if (!complete(replyParts as readonly Message[])) {
              closeMessages(replyParts as readonly Message[]);
            }
          },
          0,
          timeoutMs
        )) {
          fail(this.notReady(target.routerChannelId, 'request'));
        }
      } catch (error) {
        fail(error);
      }
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
