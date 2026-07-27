import { Message as BindingMessage } from '@zlink-systems/zlink';
import type { ActorRef } from '../../contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkSpotKind
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import type { ZLinkSpotRouteTarget } from '../spots/spot-routing-internal';
import {
  decodeActorRequestDeadlineUnixMs,
  decodeStreamHeader
} from '../streams/protocol';
import type { ZLinkActorRoutedJoinTransport } from './actor-routed-join-transport';
import { requestRoutedJsonReply } from './actor-routed-json-request';
import type { ZLinkRemoteBoundSessionTarget } from './actor-runtime-state';
import {
  encodeMessageFollowRemoteActorPacketRelayPayload,
  ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET
} from './actor-packet-relay-wire';

export const DEFAULT_MESSAGE_FOLLOW_DURATION_MS = 30_000;
const MAX_MESSAGE_FOLLOW_HOPS = 8;
const MAX_MESSAGE_FOLLOW_MESSAGES = 1024;
const MAX_MESSAGE_FOLLOW_BYTES = 16 * 1024 * 1024;
const RELOCATION_REPLY_RETENTION_MS = 24 * 60 * 60 * 1_000;

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
  readonly operationId: string;
  readonly messageFollowHopCount: number;
  readonly deadlineUnixMs?: number;
  readonly source?: ZLinkActorHandoffRequestSource;
  readonly remoteBoundSessionTarget?: {
    readonly routerChannelId: string;
    readonly targetNodeRid: string;
    readonly spotId: string;
    readonly bindingGeneration?: string;
    readonly previousAuthorityOwnerGeneration?: string;
    readonly previousOwnerLeaseGeneration?: string;
    readonly acceptedHighWater?: string;
    readonly relocationSealId?: string;
    readonly acceptedJournalReference?: string;
    readonly acceptedJournalChecksumCrc32c?: number;
  };
  readonly fallbackActorRef?: {
    readonly actorId: string;
    readonly objectGeneration: string;
    readonly meshName: string;
    readonly nodeRid: string;
  };
}

export interface ZLinkActorHandoffRequestSource {
  readonly ownerId: string;
  readonly ownerLeaseGeneration: string;
  readonly nodeRid: string;
  readonly nodeGeneration: string;
  readonly replyRouteId: string;
}

export type ZLinkActorHandoffTerminalAck =
  | 'terminalReceived'
  | 'alreadyTerminal'
  | 'notAcknowledged';

export interface ZLinkActorHandoffTerminalAcceptance {
  readonly status: ZLinkActorHandoffTerminalAck;
  readonly source?: ZLinkActorHandoffRequestSource;
}

export interface ZLinkActorHandoffResult {
  readonly index: number;
  readonly ok: boolean;
  readonly response?: unknown;
  readonly error?: string;
  readonly errorKind?: ZLinkFrameworkErrorKind;
}

interface PendingPacket {
  readonly packet: ZLinkActorHandoffPacket;
  readonly resolve?: (value: unknown) => void;
  readonly reject?: (reason: unknown) => void;
}

interface ReplyRoute {
  readonly actorId: string;
  readonly operationId: string;
  readonly source: ZLinkActorHandoffRequestSource;
  readonly resolve: (value: unknown) => void;
  readonly reject: (reason: unknown) => void;
  deadline?: ReturnType<typeof setTimeout>;
  targetNodeRid?: string;
  targetAuthorityOwnerGeneration?: bigint;
  delivered: boolean;
}

interface ActiveHandoff {
  readonly oldGeneration: bigint;
  readonly pending: PendingPacket[];
  nextIndex: number;
  snapshotIndex: number;
  pendingBytes: number;
}

interface MessageFollowRoute {
  readonly oldGeneration: bigint;
  readonly target: ZLinkSpotRouteTarget;
  readonly targetActorRef: ActorRef;
  readonly expiresAt: number;
  readonly deadline: ReturnType<typeof setTimeout>;
  tail: Promise<void>;
  queuedMessages: number;
  queuedBytes: number;
}

export interface ZLinkActorHandoffCoordinatorOptions {
  readonly routedTransport: ZLinkActorRoutedJoinTransport;
  readonly messageFollowDurationMs?: number;
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
  readonly requestSource?: () => {
    readonly ownerId: string;
    readonly ownerLeaseGeneration: bigint;
    readonly nodeRid: string;
    readonly nodeGeneration: bigint;
  } | undefined;
}

/** Owns packet ordering from relocation start through Message Follow removal. */
export class ZLinkActorHandoffCoordinator {
  private readonly active = new Map<string, ActiveHandoff>();
  private readonly messageFollowRoutes = new Map<string, MessageFollowRoute>();
  private readonly staleGenerations = new Map<string, Set<bigint>>();
  private readonly messageFollowDurationMs: number;
  private nextOperationId = 0n;
  private nextReplyRouteId = 0n;
  private readonly replyRoutes = new Map<string, ReplyRoute>();

  constructor(private readonly options: ZLinkActorHandoffCoordinatorOptions) {
    this.messageFollowDurationMs = options.messageFollowDurationMs ?? DEFAULT_MESSAGE_FOLLOW_DURATION_MS;
  }

  begin(actorId: string, oldGeneration: bigint): void {
    if (this.active.has(actorId)) {
      throw new Error(`Actor '${actorId}' already has an active packet handoff.`);
    }
    this.active.set(actorId, {
      oldGeneration,
      pending: [],
      nextIndex: 0,
      snapshotIndex: -1,
      pendingBytes: 0
    });
  }

  cancel(actorId: string): void {
    const handoff = this.active.get(actorId);
    if (handoff === undefined) return;
    this.active.delete(actorId);
    const error = new Error(`Actor '${actorId}' transfer was canceled.`);
    for (const pending of handoff.pending) {
      if (pending.packet.source !== undefined) {
        this.removeReplyRoute(pending.packet.source.replyRouteId);
      }
      pending.reject?.(error);
    }
  }

  capture(
    actorId: string,
    parts: readonly Message[],
    returnResponse = false,
    remoteBoundSessionTarget?: ZLinkRemoteBoundSessionTarget,
    fallbackActorRef?: ActorRef,
    deadlineUnixMs?: number
  ): Promise<unknown> | undefined {
    const originalDeadlineUnixMs = deadlineUnixMs
      ?? messageFollowDeadlineUnixMs(fallbackActorRef)
      ?? packetDeadlineUnixMs(parts);
    const handoff = this.active.get(actorId);
    if (handoff !== undefined) {
      if (parts.length < 2) return Promise.resolve(undefined);
      const packet = encodePacket(
        handoff.nextIndex++,
        parts,
        returnResponse,
        remoteBoundSessionTarget,
        fallbackActorRef,
        this.allocateOperationId(),
        messageFollowHopCount(fallbackActorRef),
        originalDeadlineUnixMs
      );
      this.admitBounded(handoff.pending.length, handoff.pendingBytes, packet);
      handoff.pendingBytes += packetBytes(packet);
      this.options.onMarker?.('handoff_backlog', actorId, packet.index);
      if (returnResponse) {
        this.reportRequestFrame(actorId, packet);
      }
      if (!returnResponse) {
        handoff.pending.push({ packet });
        return Promise.resolve(undefined);
      }
      return new Promise((resolve, reject) => {
        const source = this.captureRequestSource();
        const requestPacket = { ...packet, source };
        const route: ReplyRoute = {
          actorId,
          operationId: requestPacket.operationId,
          source,
          resolve,
          reject,
          delivered: false
        };
        route.deadline = setTimeout(() => {
          if (this.replyRoutes.get(source.replyRouteId) !== route) return;
          this.replyRoutes.delete(source.replyRouteId);
          if (!route.delivered) route.reject(new Error('Relocation reply route retention expired.'));
        }, RELOCATION_REPLY_RETENTION_MS);
        route.deadline.unref();
        this.replyRoutes.set(source.replyRouteId, route);
        handoff.pending.push({ packet: requestPacket, resolve, reject });
      });
    }

    const followRoute = this.messageFollowRoutes.get(actorId);
    const staleGeneration = fallbackActorRef?.objectGeneration;
    if (
      followRoute === undefined
      &&
      staleGeneration !== undefined
      && this.staleGenerations.get(actorId)?.has(staleGeneration) === true
    ) {
      this.options.onMarker?.('message_follow_expired', actorId);
      return Promise.reject(actorLocationStale(actorId));
    }
    if (
      followRoute !== undefined &&
      fallbackActorRef !== undefined &&
      (fallbackActorRef as ActorRef & { handoffMessageFollowed?: boolean })
        .handoffMessageFollowed === true &&
      this.options.isCurrentHandoffTarget?.(
        actorId,
        (fallbackActorRef as ActorRef & { handoffTargetSpotId?: string }).handoffTargetSpotId ?? ''
      ) === true
    ) {
      return undefined;
    }
    if (followRoute === undefined || !matchesGeneration(followRoute, fallbackActorRef)) {
      if (this.options.isStaleActorRef?.(actorId, fallbackActorRef) === true) {
        this.options.onMarker?.('message_follow_rejected', actorId);
        return Promise.reject(actorLocationStale(actorId));
      }
      return undefined;
    }
    if (Date.now() >= followRoute.expiresAt) {
      this.removeMessageFollowRoute(actorId, followRoute);
      this.options.onMarker?.('message_follow_rejected', actorId);
      return Promise.reject(actorLocationStale(actorId));
    }
    const packet = encodePacket(
      0,
      parts,
      returnResponse,
      remoteBoundSessionTarget,
      fallbackActorRef,
      messageFollowOperationId(fallbackActorRef) ?? this.allocateOperationId(),
      messageFollowHopCount(fallbackActorRef),
      originalDeadlineUnixMs
    );
    this.options.onMarker?.('message_follow_relay', actorId);
    return this.enqueueMessageFollow(followRoute, actorId, packet);
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
      if (pending.packet.source !== undefined) continue;
      const result = byIndex.get(pending.packet.index);
      if (result?.ok === false) {
        pending.reject?.(new Error(result.error ?? 'Actor handoff replay failed.'));
      } else if (result === undefined && pending.resolve !== undefined) {
        pending.reject?.(new Error(`Actor handoff reply '${pending.packet.index}' was not returned by the target.`));
      } else {
        pending.resolve?.(result?.response);
      }
    }

    const followRoute =
      this.installMessageFollowRoute(actorId, handoff.oldGeneration, target, targetActorRef);
    for (const pending of handoff.pending) {
      const source = pending.packet.source;
      if (source === undefined) continue;
      const route = this.replyRoutes.get(source.replyRouteId);
      if (route !== undefined) {
        route.targetNodeRid = String(target.targetNodeRid);
        route.targetAuthorityOwnerGeneration = target.authorityOwnerGeneration;
      }
    }
    for (let i = handoff.snapshotIndex + 1; i < handoff.pending.length; i++) {
      const pending = handoff.pending[i];
      if (pending.packet.source !== undefined) {
        this.removeReplyRoute(pending.packet.source.replyRouteId);
      }
      void this.enqueueMessageFollow(followRoute, actorId, pending.packet)
        .then(pending.resolve, pending.reject);
    }
  }

  messageFollowCount(actorId?: string): number {
    return actorId === undefined
      ? this.messageFollowRoutes.size
      : Number(this.messageFollowRoutes.has(actorId));
  }

  pendingCount(actorId: string): number {
    return this.active.get(actorId)?.pending.length ?? 0;
  }

  isKnownStale(actor: ActorRef): boolean {
    return this.messageFollowRoutes.has(actor.actorId) === false
      && this.staleGenerations.get(actor.actorId)?.has(actor.objectGeneration) === true;
  }

  recordStaleFailure(actorId: string): void {
    this.options.onMarker?.('message_follow_rejected', actorId);
  }

  acceptRelocatedTerminal(
    actorId: string,
    packet: ZLinkActorHandoffPacket,
    result: ZLinkActorHandoffResult,
    sourceNodeRid: string,
    targetAuthorityOwnerGeneration: bigint | undefined
  ): ZLinkActorHandoffTerminalAck {
    const source = packet.source;
    if (source === undefined || BigInt(source.replyRouteId) <= 0n) return 'notAcknowledged';
    return this.acceptRelocatedTerminalRelay(
      packet.operationId,
      source.replyRouteId,
      source,
      result,
      sourceNodeRid,
      targetAuthorityOwnerGeneration,
      actorId
    ).status;
  }

  acceptRelocatedTerminalRelay(
    operationId: string,
    replyRouteId: string,
    source: ZLinkActorHandoffRequestSource | undefined,
    result: ZLinkActorHandoffResult,
    sourceNodeRid: string,
    targetAuthorityOwnerGeneration: bigint | undefined,
    actorId?: string
  ): ZLinkActorHandoffTerminalAcceptance {
    if (BigInt(replyRouteId) <= 0n) return { status: 'notAcknowledged' };
    const currentSource = this.options.requestSource?.();
    const route = this.replyRoutes.get(replyRouteId);
    const exactSource = source ?? route?.source;
    if (currentSource === undefined || route === undefined || exactSource === undefined
      || currentSource.ownerId !== exactSource.ownerId
      || currentSource.ownerLeaseGeneration !== BigInt(exactSource.ownerLeaseGeneration)
      || currentSource.nodeRid !== exactSource.nodeRid
      || currentSource.nodeGeneration !== BigInt(exactSource.nodeGeneration)
      || (actorId !== undefined && route.actorId !== actorId)
      || route.operationId !== operationId
      || route.targetNodeRid !== sourceNodeRid
      || (targetAuthorityOwnerGeneration !== undefined
        && route.targetAuthorityOwnerGeneration !== targetAuthorityOwnerGeneration)) {
      return { status: 'notAcknowledged' };
    }
    if (route.delivered) return { status: 'alreadyTerminal', source: route.source };
    route.delivered = true;
    if (result.ok) {
      route.resolve(result.response);
    } else if (result.errorKind === ZLinkFrameworkErrorKind.DeadlineExceeded) {
      route.reject(actorDeadlineExceeded(route.actorId));
    } else {
      route.reject(new Error(result.error ?? 'Actor handoff replay failed.'));
    }
    return { status: 'terminalReceived', source: route.source };
  }

  private captureRequestSource(): ZLinkActorHandoffRequestSource {
    const source = this.options.requestSource?.();
    if (source === undefined || source.ownerId.length === 0
      || source.ownerLeaseGeneration <= 0n || source.nodeRid.length === 0
      || source.nodeGeneration <= 0n) {
      throw new Error('Actor handoff request requires an exact source owner fence.');
    }
    this.nextReplyRouteId += 1n;
    if (this.nextReplyRouteId <= 0n) throw new Error('Actor handoff ReplyRouteId is exhausted.');
    return {
      ownerId: source.ownerId,
      ownerLeaseGeneration: source.ownerLeaseGeneration.toString(),
      nodeRid: source.nodeRid,
      nodeGeneration: source.nodeGeneration.toString(),
      replyRouteId: this.nextReplyRouteId.toString()
    };
  }

  private removeReplyRoute(replyRouteId: string): void {
    const route = this.replyRoutes.get(replyRouteId);
    if (route === undefined) return;
    if (route.deadline !== undefined) clearTimeout(route.deadline);
    this.replyRoutes.delete(replyRouteId);
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

  private installMessageFollowRoute(
    actorId: string,
    oldGeneration: bigint,
    target: ZLinkSpotRouteTarget,
    targetActorRef: ActorRef
  ): MessageFollowRoute {
    let stale = this.staleGenerations.get(actorId);
    if (stale === undefined) {
      stale = new Set();
      this.staleGenerations.set(actorId, stale);
    }
    stale.add(oldGeneration);
    const previous = this.messageFollowRoutes.get(actorId);
    if (previous !== undefined) clearTimeout(previous.deadline);
    const expiresAt = Date.now() + this.messageFollowDurationMs;
    const entry = {
      oldGeneration,
      target,
      targetActorRef,
      expiresAt,
      deadline: undefined as unknown as ReturnType<typeof setTimeout>,
      tail: Promise.resolve(),
      queuedMessages: 0,
      queuedBytes: 0
    };
    entry.deadline = setTimeout(
      () => this.removeMessageFollowRoute(actorId, entry),
      this.messageFollowDurationMs
    );
    entry.deadline.unref();
    this.messageFollowRoutes.set(actorId, entry);
    this.options.onMarker?.('message_follow_registered', actorId, this.messageFollowDurationMs);
    return entry;
  }

  private removeMessageFollowRoute(actorId: string, entry: MessageFollowRoute): void {
    if (this.messageFollowRoutes.get(actorId) !== entry) return;
    clearTimeout(entry.deadline);
    this.messageFollowRoutes.delete(actorId);
    this.options.onMarker?.('message_follow_route_removed', actorId);
  }

  private enqueueMessageFollow(
    entry: MessageFollowRoute,
    actorId: string,
    packet: ZLinkActorHandoffPacket
  ): Promise<unknown> {
    const bytes = packetBytes(packet);
    this.admitBounded(entry.queuedMessages, entry.queuedBytes, packet);
    if (packet.messageFollowHopCount >= MAX_MESSAGE_FOLLOW_HOPS) {
      return Promise.reject(actorLocationStale(actorId));
    }
    entry.queuedMessages++;
    entry.queuedBytes += bytes;
    let resolve!: (value: unknown) => void;
    let reject!: (reason: unknown) => void;
    const result = new Promise<unknown>((done, fail) => {
      resolve = done;
      reject = fail;
    });
    entry.tail = entry.tail.then(async () => {
      try {
        resolve(await this.relayMessageFollow(actorId, entry.target, entry.targetActorRef, packet));
      } catch (error) {
        reject(error);
      } finally {
        entry.queuedMessages--;
        entry.queuedBytes -= bytes;
      }
    });
    return result;
  }

  private allocateOperationId(): string {
    return `0:${++this.nextOperationId}`;
  }

  private admitBounded(messageCount: number, byteCount: number, packet: ZLinkActorHandoffPacket): void {
    if (
      messageCount >= MAX_MESSAGE_FOLLOW_MESSAGES
      || byteCount + packetBytes(packet) > MAX_MESSAGE_FOLLOW_BYTES
    ) {
      throw actorLocationStale('message-follow-bound');
    }
  }

  private async relayMessageFollow(
    actorId: string,
    target: ZLinkSpotRouteTarget,
    targetActorRef: ActorRef,
    packet: ZLinkActorHandoffPacket
  ): Promise<unknown> {
    const payload = encodeMessageFollowRemoteActorPacketRelayPayload({
      actorId,
      routerChannelId: packet.remoteBoundSessionTarget?.routerChannelId,
      boundSessionTargetNodeRid: packet.remoteBoundSessionTarget?.targetNodeRid,
      boundSessionSpotId: packet.remoteBoundSessionTarget?.spotId,
      header: packet.header,
      payload: packet.payload,
      actorNodeRid: String(targetActorRef.nodeRid),
      actorGeneration: targetActorRef.objectGeneration.toString(),
      handoffTargetSpotId: String(target.spotId),
      operationId: packet.operationId,
      messageFollowHopCount: packet.messageFollowHopCount + 1,
      deadlineUnixMs: packet.deadlineUnixMs,
      authorityOwnerGeneration: target.authorityOwnerGeneration?.toString(),
      ownerLeaseGeneration: target.ownerLeaseGeneration?.toString()
    });
    if (!packet.returnResponse) {
      await this.options.routedTransport.sendToSpot(target, payload, {
        packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET
      });
      return undefined;
    }
    const remainingMs = remainingRequestTime(actorId, packet.deadlineUnixMs);
    if (this.options.routedTransport.requestRawToSpot === undefined) {
      const reply = await awaitBeforeDeadline(
        this.options.routedTransport.requestToSpot<Record<string, unknown>>(target, payload, {
          packetName: ZLINK_REMOTE_ACTOR_PACKET_RELAY_PACKET,
          timeoutMs: remainingMs ?? this.options.requestTimeoutMs
        }),
        actorId,
        remainingMs
      );
      if (reply.ok === false) {
        if (reply.errorKind === ZLinkFrameworkErrorKind.DeadlineExceeded) {
          throw actorDeadlineExceeded(actorId);
        }
        throw new Error(String(reply.error ?? 'Actor Message Follow relay failed.'));
      }
      return reply.response;
    }
    return await awaitBeforeDeadline(
      requestRoutedJsonReply(
        this.options.routedTransport,
        target,
        payload,
        { timeoutMs: remainingMs ?? this.options.requestTimeoutMs },
        `Actor Message Follow raw request is not available for '${actorId}'.`,
        (parts) => {
          if (parts.length === 0) {
            throw new Error(`Actor Message Follow reply was empty for '${actorId}'.`);
          }
          const reply = JSON.parse(parts[0].getString('utf8')) as {
            readonly ok?: boolean;
            readonly response?: unknown;
            readonly error?: unknown;
            readonly errorKind?: unknown;
          };
          if (reply.ok === false) {
            if (reply.errorKind === ZLinkFrameworkErrorKind.DeadlineExceeded) {
              throw actorDeadlineExceeded(actorId);
            }
            throw new Error(String(reply.error ?? 'Actor Message Follow relay failed.'));
          }
          return reply.response;
        }
      ),
      actorId,
      remainingMs
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
          spotId: packet.remoteBoundSessionTarget.spotId,
          bindingGeneration: optionalBigInt(packet.remoteBoundSessionTarget.bindingGeneration),
          previousAuthorityOwnerGeneration:
            optionalBigInt(packet.remoteBoundSessionTarget.previousAuthorityOwnerGeneration),
          previousOwnerLeaseGeneration:
            optionalBigInt(packet.remoteBoundSessionTarget.previousOwnerLeaseGeneration),
          acceptedHighWater: optionalBigInt(packet.remoteBoundSessionTarget.acceptedHighWater),
          relocationSealId: packet.remoteBoundSessionTarget.relocationSealId,
          acceptedJournalReference: packet.remoteBoundSessionTarget.acceptedJournalReference,
          acceptedJournalChecksumCrc32c: packet.remoteBoundSessionTarget.acceptedJournalChecksumCrc32c
        },
    fallbackActorRef: packet.fallbackActorRef === undefined
      ? undefined
      : {
          actorId: packet.fallbackActorRef.actorId,
          objectGeneration: BigInt(packet.fallbackActorRef.objectGeneration),
          meshName: packet.fallbackActorRef.meshName,
          nodeRid: packet.fallbackActorRef.nodeRid
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
      if (
        packet.returnResponse
        && packet.deadlineUnixMs !== undefined
        && Date.now() >= packet.deadlineUnixMs
      ) {
        throw actorDeadlineExceeded(packet.fallbackActorRef?.actorId ?? 'accepted-handoff');
      }
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
        error: error instanceof Error ? error.message : String(error),
        errorKind: error instanceof ZLinkFrameworkException ? error.kind : undefined
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
  fallbackActorRef?: ActorRef,
  operationId = '',
  messageFollowHopCount = 0,
  deadlineUnixMs?: number
): ZLinkActorHandoffPacket {
  return {
    index,
    header: Buffer.from(parts[0].data()).toString('base64'),
    payload: Buffer.from(parts[1].data()).toString('base64'),
    returnResponse,
    operationId,
    messageFollowHopCount,
    deadlineUnixMs,
    remoteBoundSessionTarget: remoteBoundSessionTarget === undefined
      ? undefined
      : {
          routerChannelId: remoteBoundSessionTarget.routerChannelId,
          targetNodeRid: String(remoteBoundSessionTarget.targetNodeRid),
          spotId: String(remoteBoundSessionTarget.spotId),
          bindingGeneration: remoteBoundSessionTarget.bindingGeneration?.toString(),
          previousAuthorityOwnerGeneration:
            remoteBoundSessionTarget.previousAuthorityOwnerGeneration?.toString(),
          previousOwnerLeaseGeneration:
            remoteBoundSessionTarget.previousOwnerLeaseGeneration?.toString(),
          acceptedHighWater: remoteBoundSessionTarget.acceptedHighWater?.toString(),
          relocationSealId: remoteBoundSessionTarget.relocationSealId,
          acceptedJournalReference: remoteBoundSessionTarget.acceptedJournalReference,
          acceptedJournalChecksumCrc32c: remoteBoundSessionTarget.acceptedJournalChecksumCrc32c
        },
    fallbackActorRef: fallbackActorRef === undefined
      ? undefined
      : {
          actorId: fallbackActorRef.actorId,
          objectGeneration: fallbackActorRef.objectGeneration.toString(),
          meshName: fallbackActorRef.meshName,
          nodeRid: String(fallbackActorRef.nodeRid)
        }
  };
}

function optionalBigInt(value: string | undefined): bigint | undefined {
  return value === undefined ? undefined : BigInt(value);
}

function packetBytes(packet: ZLinkActorHandoffPacket): number {
  return Buffer.byteLength(packet.header, 'base64') + Buffer.byteLength(packet.payload, 'base64');
}

function packetDeadlineUnixMs(parts: readonly Message[]): number | undefined {
  if (parts.length === 0) return undefined;
  try {
    return decodeActorRequestDeadlineUnixMs(parts[0].data());
  } catch {
    return undefined;
  }
}

function messageFollowOperationId(actorRef: ActorRef | undefined): string | undefined {
  return (actorRef as ActorRef & { handoffOperationId?: string } | undefined)?.handoffOperationId;
}

function messageFollowHopCount(actorRef: ActorRef | undefined): number {
  return (actorRef as ActorRef & { handoffMessageFollowHopCount?: number } | undefined)
    ?.handoffMessageFollowHopCount ?? 0;
}

function messageFollowDeadlineUnixMs(actorRef: ActorRef | undefined): number | undefined {
  return (actorRef as ActorRef & { handoffDeadlineUnixMs?: number } | undefined)
    ?.handoffDeadlineUnixMs;
}

function remainingRequestTime(actorId: string, deadlineUnixMs: number | undefined): number | undefined {
  if (deadlineUnixMs === undefined) return undefined;
  const remaining = deadlineUnixMs - Date.now();
  if (remaining <= 0) throw actorDeadlineExceeded(actorId);
  return Math.max(1, Math.ceil(remaining));
}

async function awaitBeforeDeadline<T>(
  operation: Promise<T>,
  actorId: string,
  remainingMs: number | undefined
): Promise<T> {
  if (remainingMs === undefined) return await operation;
  let timer: ReturnType<typeof setTimeout> | undefined;
  try {
    return await Promise.race([
      operation,
      new Promise<never>((_resolve, reject) => {
        timer = setTimeout(() => reject(actorDeadlineExceeded(actorId)), remainingMs);
      })
    ]);
  } finally {
    if (timer !== undefined) clearTimeout(timer);
  }
}

function matchesGeneration(entry: MessageFollowRoute, actorRef: ActorRef | undefined): boolean {
  return actorRef === undefined || actorRef.objectGeneration === entry.oldGeneration;
}

function actorLocationStale(actorId: string): ZLinkFrameworkException {
  return new ZLinkFrameworkException(
    ZLinkFrameworkErrorKind.ActorLocationStale,
    `Actor route '${actorId}' is stale after the Message Follow duration.`
  );
}

function actorDeadlineExceeded(actorId: string): ZLinkFrameworkException {
  return new ZLinkFrameworkException(
    ZLinkFrameworkErrorKind.DeadlineExceeded,
    `Actor request '${actorId}' exceeded its original deadline during Message Follow.`
  );
}
