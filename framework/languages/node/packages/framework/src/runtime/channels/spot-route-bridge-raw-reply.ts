import type { Message } from '@zlink-systems/zlink';
import {
  ZLinkConfigurationException
} from '../configuration';
import {
  isSpotRouteBridgeReplyPayload
} from '../spots/route-wire-codec';
import {
  closeMessages
} from './channel-envelope';

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
 * replies (FIFO) to the oldest pending request.
 */
export class ZLinkSpotRouteBridgeRawReplyRegistry {
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

function looksLikeRawSpotRouteBridgeReply(parts: readonly Message[]): boolean {
  if (parts.length === 0) {
    return false;
  }
  try {
    const decoded = JSON.parse(parts[0].data().toString('utf8')) as unknown;
    return isSpotRouteBridgeReplyPayload(decoded);
  } catch {
    return false;
  }
}
