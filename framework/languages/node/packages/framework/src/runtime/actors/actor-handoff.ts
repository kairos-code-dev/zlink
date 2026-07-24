import { Message as BindingMessage } from '@zlink-systems/zlink';
import type { ActorRef } from '../../contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkSpotKind
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import type { ZLinkSpotRouteTarget } from '../spots/spot-routing-internal';
import { decodeStreamHeader } from '../streams/protocol';
import type { ZLinkActorRoutedJoinTransport } from './actor-routed-join-transport';
import { requestRoutedJsonReply } from './actor-routed-json-request';
import type { ZLinkRemoteBoundSessionTarget } from './actor-runtime-state';
import {
  encodeForwardedRemoteActorPacketRelayPayload,
  ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET
} from './actor-packet-relay-wire';

export const DEFAULT_ACTOR_TRANSFER_FORWARD_WINDOW_MS = 5_000;

export interface ZLinkActorHandoffTarget {
  readonly routerChannelId: string;
  readonly targetNodeRid: string;
  readonly spotId: string;
  readonly spotKind?: ZLinkSpotKind;
}

export interface ZLinkActorHandoffPacket {
  readonly index: number;
  readonly header: string;
  readonly payload: string;
  readonly returnResponse: boolean;
  readonly remoteBoundSessionTarget?: {
    readonly routerChannelId: string;
    readonly targetNodeRid: string;
    readonly spotId: string;
  };
  readonly fallbackActorRef?: {
    readonly nodeRid: string;
    readonly actorId: string;
    readonly generation: string;
  };
}

export interface ZLinkActorHandoffResult {
  readonly index: number;
  readonly ok: boolean;
  readonly response?: unknown;
  readonly error?: string;
}

interface PendingPacket {
  readonly packet: ZLinkActorHandoffPacket;
  readonly resolve?: (value: unknown) => void;
  readonly reject?: (reason: unknown) => void;
}

interface ActiveHandoff {
  readonly oldGeneration: bigint;
  readonly pending: PendingPacket[];
  nextIndex: number;
  snapshotIndex: number;
}

interface ForwardingEntry {
  readonly oldGeneration: bigint;
  readonly target: ZLinkSpotRouteTarget;
  readonly targetActorRef: ActorRef;
  readonly expiresAt: number;
  readonly deadline: ReturnType<typeof setTimeout>;
  tail: Promise<void>;
}

export interface ZLinkActorHandoffCoordinatorOptions {
  readonly routedTransport: ZLinkActorRoutedJoinTransport;
  readonly forwardActorPacket?: (
    actorId: string,
    actor: ActorRef,
    packet: ZLinkActorHandoffPacket
  ) => Promise<unknown>;
  readonly forwardWindowMs?: number;
  readonly requestTimeoutMs?: number;
  readonly onMarker?: (marker: string, actorId: string, index?: number) => void;
  readonly onRequestFrame?: (
    actorId: string,
    index: number,
    requestSeq: bigint | undefined,
    flags: number
  ) => void;
  readonly isStaleActorRef?: (actorId: string, actorRef?: ActorRef) => boolean;
  readonly isCurrentHandoffTarget?: (actorId: string, spotId: string) => boolean;
}

/** Owns the packet-order boundary from moving start through forwarding cutoff. */
export class ZLinkActorHandoffCoordinator {
  private readonly active = new Map<string, ActiveHandoff>();
  private readonly forwarding = new Map<string, ForwardingEntry>();
  private readonly staleGenerations = new Map<string, Set<bigint>>();
  private readonly forwardWindowMs: number;

  constructor(private readonly options: ZLinkActorHandoffCoordinatorOptions) {
    this.forwardWindowMs = options.forwardWindowMs ?? DEFAULT_ACTOR_TRANSFER_FORWARD_WINDOW_MS;
  }

  begin(actorId: string, oldGeneration: bigint): void {
    if (this.active.has(actorId)) {
      throw new Error(`Actor '${actorId}' already has an active packet handoff.`);
    }
    this.active.set(actorId, {
      oldGeneration,
      pending: [],
      nextIndex: 0,
      snapshotIndex: -1
    });
  }

  cancel(actorId: string): void {
    const handoff = this.active.get(actorId);
    if (handoff === undefined) return;
    this.active.delete(actorId);
    const error = new Error(`Actor '${actorId}' transfer was canceled.`);
    for (const pending of handoff.pending) pending.reject?.(error);
  }

  capture(
    actorId: string,
    parts: readonly Message[],
    returnResponse = false,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ): Promise<unknown> | undefined {
    const handoff = this.active.get(actorId);
    if (handoff !== undefined) {
      if (parts.length < 2) return Promise.resolve(undefined);
      const packet = encodePacket(
        handoff.nextIndex++,
        parts,
        returnResponse,
        remoteBoundSessionTarget,
        fallbackActorRef
      );
      this.options.onMarker?.('handoff_backlog', actorId, packet.index);
      if (returnResponse) {
        this.reportRequestFrame(actorId, packet);
      }
      if (!returnResponse) {
        handoff.pending.push({ packet });
        return Promise.resolve(undefined);
      }
      return new Promise((resolve, reject) => {
        handoff.pending.push({ packet, resolve, reject });
      });
    }

    const forwarding = this.forwarding.get(actorId);
    const staleGeneration = fallbackActorRef?.generation;
    if (
      forwarding === undefined
      &&
      staleGeneration !== undefined
      && this.staleGenerations.get(actorId)?.has(staleGeneration) === true
    ) {
      this.options.onMarker?.('stale_fail_fast', actorId);
      return Promise.reject(actorLocationStale(actorId));
    }
    if (
      forwarding !== undefined &&
      fallbackActorRef !== undefined &&
      (fallbackActorRef as ActorRef & { handoffForwarded?: boolean }).handoffForwarded === true &&
      this.options.isCurrentHandoffTarget?.(
        actorId,
        (fallbackActorRef as ActorRef & { handoffTargetSpotId?: string }).handoffTargetSpotId ?? ''
      ) === true
    ) {
      return undefined;
    }
    if (forwarding === undefined || !matchesGeneration(forwarding, fallbackActorRef)) {
      if (this.options.isStaleActorRef?.(actorId, fallbackActorRef) === true) {
        this.options.onMarker?.('stale_fail_fast', actorId);
        return Promise.reject(actorLocationStale(actorId));
      }
      return undefined;
    }
    if (Date.now() >= forwarding.expiresAt) {
      this.evict(actorId, forwarding);
      this.options.onMarker?.('stale_fail_fast', actorId);
      return Promise.reject(actorLocationStale(actorId));
    }
    const packet = encodePacket(0, parts, returnResponse, remoteBoundSessionTarget, fallbackActorRef);
    this.options.onMarker?.('straggler_forward', actorId);
    return this.enqueueForward(forwarding, actorId, packet);
  }

  snapshot(actorId: string): readonly ZLinkActorHandoffPacket[] {
    const handoff = this.requireActive(actorId);
    handoff.snapshotIndex = handoff.pending.length - 1;
    return handoff.pending.map((entry) => entry.packet);
  }

  snapshotCoreBacklog(actorId: string): readonly ZLinkActorHandoffPacket[] {
    const handoff = this.requireActive(actorId);
    let snapshotIndex = -1;
    while (
      snapshotIndex + 1 < handoff.pending.length
      && handoff.pending[snapshotIndex + 1].packet.returnResponse === false
    ) {
      snapshotIndex++;
    }
    handoff.snapshotIndex = snapshotIndex;
    return handoff.pending.slice(0, snapshotIndex + 1).map((entry) => entry.packet);
  }

  complete(
    actorId: string,
    target: ZLinkSpotRouteTarget,
    targetActorRef: ActorRef,
    results: readonly ZLinkActorHandoffResult[] = []
  ): void {
    const handoff = this.requireActive(actorId);
    this.active.delete(actorId);
    const byIndex = new Map(results.map((result) => [result.index, result]));
    for (let i = 0; i <= handoff.snapshotIndex; i++) {
      const pending = handoff.pending[i];
      const result = byIndex.get(pending.packet.index);
      if (result?.ok === false) {
        pending.reject?.(new Error(result.error ?? 'Actor handoff replay failed.'));
      } else if (result === undefined && pending.resolve !== undefined) {
        pending.reject?.(new Error(`Actor handoff reply '${pending.packet.index}' was not returned by the target.`));
      } else {
        pending.resolve?.(result?.response);
      }
    }

    const forwarding = this.installForwarding(actorId, handoff.oldGeneration, target, targetActorRef);
    for (let i = handoff.snapshotIndex + 1; i < handoff.pending.length; i++) {
      const pending = handoff.pending[i];
      void this.enqueueForward(forwarding, actorId, pending.packet).then(pending.resolve, pending.reject);
    }
  }

  forwardingCount(actorId?: string): number {
    return actorId === undefined ? this.forwarding.size : Number(this.forwarding.has(actorId));
  }

  pendingCount(actorId: string): number {
    return this.active.get(actorId)?.pending.length ?? 0;
  }

  isKnownStale(actor: ActorRef): boolean {
    return this.forwarding.has(actor.actorId) === false
      && this.staleGenerations.get(actor.actorId)?.has(actor.generation) === true;
  }

  recordStaleFailure(actorId: string): void {
    this.options.onMarker?.('stale_fail_fast', actorId);
  }

  private reportRequestFrame(actorId: string, packet: ZLinkActorHandoffPacket): void {
    try {
      const header = decodeStreamHeader(Buffer.from(packet.header, 'base64'));
      this.options.onRequestFrame?.(actorId, packet.index, header.requestSeq, header.flags);
    } catch {
      // Evidence collection must not change how a malformed packet is replayed and rejected.
    }
  }

  private requireActive(actorId: string): ActiveHandoff {
    const handoff = this.active.get(actorId);
    if (handoff === undefined) throw new Error(`Actor '${actorId}' does not have an active packet handoff.`);
    return handoff;
  }

  private installForwarding(
    actorId: string,
    oldGeneration: bigint,
    target: ZLinkSpotRouteTarget,
    targetActorRef: ActorRef
  ): ForwardingEntry {
    let stale = this.staleGenerations.get(actorId);
    if (stale === undefined) {
      stale = new Set();
      this.staleGenerations.set(actorId, stale);
    }
    stale.add(oldGeneration);
    const previous = this.forwarding.get(actorId);
    if (previous !== undefined) clearTimeout(previous.deadline);
    const expiresAt = Date.now() + this.forwardWindowMs;
    const entry = {
      oldGeneration,
      target,
      targetActorRef,
      expiresAt,
      deadline: undefined as unknown as ReturnType<typeof setTimeout>,
      tail: Promise.resolve()
    };
    entry.deadline = setTimeout(() => this.evict(actorId, entry), this.forwardWindowMs);
    entry.deadline.unref();
    this.forwarding.set(actorId, entry);
    this.options.onMarker?.('forwarding_mapped', actorId, this.forwardWindowMs);
    return entry;
  }

  private evict(actorId: string, entry: ForwardingEntry): void {
    if (this.forwarding.get(actorId) !== entry) return;
    clearTimeout(entry.deadline);
    this.forwarding.delete(actorId);
    this.options.onMarker?.('mapping_evicted', actorId);
  }

  private enqueueForward(
    entry: ForwardingEntry,
    actorId: string,
    packet: ZLinkActorHandoffPacket
  ): Promise<unknown> {
    let resolve!: (value: unknown) => void;
    let reject!: (reason: unknown) => void;
    const result = new Promise<unknown>((done, fail) => {
      resolve = done;
      reject = fail;
    });
    entry.tail = entry.tail.then(async () => {
      try {
        resolve(await this.forward(actorId, entry.target, entry.targetActorRef, packet));
      } catch (error) {
        reject(error);
      }
    });
    return result;
  }

  private async forward(
    actorId: string,
    target: ZLinkSpotRouteTarget,
    targetActorRef: ActorRef,
    packet: ZLinkActorHandoffPacket
  ): Promise<unknown> {
    if (this.options.forwardActorPacket !== undefined) {
      return await this.options.forwardActorPacket(actorId, targetActorRef, packet);
    }
    const payload = encodeForwardedRemoteActorPacketRelayPayload({
      actorId,
      routerChannelId: packet.remoteBoundSessionTarget?.routerChannelId,
      boundSessionTargetNodeRid: packet.remoteBoundSessionTarget?.targetNodeRid,
      boundSessionSpotId: packet.remoteBoundSessionTarget?.spotId,
      header: packet.header,
      payload: packet.payload,
      actorNodeRid: String(targetActorRef.nodeRid),
      actorGeneration: targetActorRef.generation.toString(),
      handoffTargetSpotId: String(target.spotId)
    });
    if (!packet.returnResponse) {
      await this.options.routedTransport.sendToSpot(target, payload, {
        packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET
      });
      return undefined;
    }
    if (this.options.routedTransport.requestRawToSpot === undefined) {
      const reply = await this.options.routedTransport.requestToSpot<Record<string, unknown>>(target, payload, {
        packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
        timeoutMs: this.options.requestTimeoutMs
      });
      if (reply.ok === false) throw new Error(String(reply.error ?? 'Actor packet forwarding failed.'));
      return reply.response;
    }
    return await requestRoutedJsonReply(
      this.options.routedTransport,
      target,
      payload,
      { timeoutMs: this.options.requestTimeoutMs },
      `Actor packet forwarding raw request is not available for '${actorId}'.`,
      (parts) => {
        if (parts.length === 0) throw new Error(`Actor packet forwarding reply was empty for '${actorId}'.`);
        const reply = JSON.parse(parts[0].getString('utf8')) as {
          readonly ok?: boolean;
          readonly response?: unknown;
          readonly error?: unknown;
        };
        if (reply.ok === false) throw new Error(String(reply.error ?? 'Actor packet forwarding failed.'));
        return reply.response;
      }
    );
  }
}

export function decodeHandoffPacket(packet: ZLinkActorHandoffPacket): {
  readonly parts: readonly Message[];
  readonly remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget;
  readonly fallbackActorRef?: ActorRef;
} {
  return {
    parts: [
      BindingMessage.from(Buffer.from(packet.header, 'base64')) as Message,
      BindingMessage.from(Buffer.from(packet.payload, 'base64')) as Message
    ],
    remoteBoundSessionTarget: packet.remoteBoundSessionTarget === undefined
      ? undefined
      : {
          routerChannelId: packet.remoteBoundSessionTarget.routerChannelId,
          targetNodeRid: packet.remoteBoundSessionTarget.targetNodeRid,
          spotId: packet.remoteBoundSessionTarget.spotId
        },
    fallbackActorRef: packet.fallbackActorRef === undefined
      ? undefined
      : {
          nodeRid: packet.fallbackActorRef.nodeRid,
          actorId: packet.fallbackActorRef.actorId,
          generation: BigInt(packet.fallbackActorRef.generation)
        }
  };
}

export async function replayActorHandoffBacklog(
  backlog: readonly ZLinkActorHandoffPacket[],
  dispatch: (
    parts: readonly Message[],
    returnResponse: boolean,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef
  ) => Promise<unknown>,
  onEnqueued?: (index: number) => Promise<void> | void
): Promise<readonly ZLinkActorHandoffResult[]> {
  const results: ZLinkActorHandoffResult[] = [];
  for (const packet of backlog) {
    const decoded = decodeHandoffPacket(packet);
    try {
      await onEnqueued?.(packet.index);
      const response = await dispatch(
        decoded.parts,
        packet.returnResponse,
        decoded.remoteBoundSessionTarget,
        decoded.fallbackActorRef
      );
      results.push({ index: packet.index, ok: true, response });
    } catch (error) {
      results.push({
        index: packet.index,
        ok: false,
        error: error instanceof Error ? error.message : String(error)
      });
    } finally {
      decoded.parts.forEach((part) => part.close());
    }
  }
  return results;
}

function encodePacket(
  index: number,
  parts: readonly Message[],
  returnResponse: boolean,
  remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
  fallbackActorRef?: ActorRef
): ZLinkActorHandoffPacket {
  return {
    index,
    header: Buffer.from(parts[0].data()).toString('base64'),
    payload: Buffer.from(parts[1].data()).toString('base64'),
    returnResponse,
    remoteBoundSessionTarget: remoteBoundSessionTarget === undefined
      ? undefined
      : {
          routerChannelId: remoteBoundSessionTarget.routerChannelId,
          targetNodeRid: String(remoteBoundSessionTarget.targetNodeRid),
          spotId: String(remoteBoundSessionTarget.spotId)
        },
    fallbackActorRef: fallbackActorRef === undefined
      ? undefined
      : {
          nodeRid: String(fallbackActorRef.nodeRid),
          actorId: fallbackActorRef.actorId,
          generation: fallbackActorRef.generation.toString()
        }
  };
}

function matchesGeneration(entry: ForwardingEntry, actorRef: ActorRef | undefined): boolean {
  return actorRef === undefined || actorRef.generation === entry.oldGeneration;
}

function actorLocationStale(actorId: string): ZLinkFrameworkException {
  return new ZLinkFrameworkException(
    ZLinkFrameworkErrorKind.ActorLocationStale,
    `Actor route '${actorId}' is stale after the transfer forwarding window.`
  );
}
