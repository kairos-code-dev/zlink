// SPDX-License-Identifier: MPL-2.0

import { RoutingId } from '../../contracts/core';
import type { MessageLike } from '../../contracts/messaging';
import type { ActorLocation, ActorRef, ReceiveKindData } from '../../contracts/service';
import type { ActorLocationRaw, ActorRefRaw, MeshKindDataRaw } from '../native/binding_service_types';
import { normalizeRoutingId } from '../core/routing_id';
import { wrapRoutingId } from '../core/routing_id_conversion';
import { normalizeMessageLikePayload } from '../buffers/message_conversion';

/** Convert a native routing id buffer to a public RoutingId, or null when empty. */
export function ridOrNull(bytes: Buffer | null | undefined): RoutingId | null {
  return wrapRoutingId(bytes);
}

/** Convert a native actor reference to its public value type. */
export function actorRefFromRaw(raw: ActorRefRaw): ActorRef {
  return {
    nodeRid: RoutingId.from(raw.nodeRid),
    actorId: raw.actorId,
    generation: raw.generation
  };
}

/** Convert a native actor reference to its public value type, or null when unset. */
export function maybeActorRefFromRaw(raw: ActorRefRaw): ActorRef | null {
  if (!raw || !raw.actorId || raw.nodeRid == null || raw.nodeRid.length === 0) {
    return null;
  }
  return actorRefFromRaw(raw);
}

/** Convert a public actor reference to its native marshaling shape. */
export function actorRefToRaw(actor: ActorRef): ActorRefRaw {
  return {
    nodeRid: normalizeRoutingId(actor.nodeRid, 'actor.nodeRid'),
    actorId: actor.actorId,
    generation: actor.generation
  };
}

/** Convert a native actor location to its public value type. */
export function actorLocationFromRaw(raw: ActorLocationRaw): ActorLocation {
  return {
    actor: actorRefFromRaw(raw.actor),
    spotRid: ridOrNull(raw.spotRid),
    spotGeneration: raw.spotGeneration,
    membershipEpoch: raw.membershipEpoch
  };
}

/** Convert a native typed kind_data payload to its public value type, or null when absent. */
export function kindDataFromRaw(raw: MeshKindDataRaw | null | undefined): ReceiveKindData | null {
  if (!raw) {
    return null;
  }
  switch (raw.kind) {
    case 'actorControl':
      return {
        kind: 'actorControl',
        lifecycleKind: raw.lifecycleKind,
        previousActor: maybeActorRefFromRaw(raw.previousActor),
        currentActor: maybeActorRefFromRaw(raw.currentActor),
        previousSpotRid: ridOrNull(raw.previousSpotRid),
        currentSpotRid: ridOrNull(raw.currentSpotRid),
        previousSpotGeneration: raw.previousSpotGeneration,
        currentSpotGeneration: raw.currentSpotGeneration,
        previousMembershipEpoch: raw.previousMembershipEpoch,
        currentMembershipEpoch: raw.currentMembershipEpoch,
        resultCode: raw.resultCode
      };
    case 'actorJoinCompletion':
      return {
        kind: 'actorJoinCompletion',
        joinResult: raw.joinResult,
        actor: maybeActorRefFromRaw(raw.actor),
        location: actorLocationFromRaw(raw.location)
      };
    case 'actorLookupCompletion':
      return {
        kind: 'actorLookupCompletion',
        location: actorLocationFromRaw(raw.location)
      };
    case 'sendReady':
      return {
        kind: 'sendReady',
        destinationKind: raw.destinationKind,
        targetNodeRid: ridOrNull(raw.targetNodeRid),
        targetSpotRid: ridOrNull(raw.targetSpotRid),
        targetActor: maybeActorRefFromRaw(raw.targetActor),
        channelName: raw.channelName
      };
    case 'transferControl':
      return {
        kind: 'transferControl',
        phase: raw.phase,
        role: raw.role,
        transferId: { high: raw.transferId.high, low: raw.transferId.low },
        actor: maybeActorRefFromRaw(raw.actor),
        membershipEpoch: raw.membershipEpoch,
        finalSequence: raw.finalSequence,
        resultCode: raw.resultCode,
        failureErrno: raw.failureErrno
      };
    default:
      return null;
  }
}

/** Normalize an optional metadata buffer for a native call. */
export function metadataOrNull(metadata: Buffer | undefined): Buffer | null {
  return metadata ?? null;
}

/** Normalize optional send flags to a native flags integer. */
export function flagsOrZero(flags: number | undefined): number {
  return (flags ?? 0) | 0;
}

/** Normalize an optional timeout to a native milliseconds integer. */
export function timeoutOrZero(timeoutMs: number | undefined): number {
  return (timeoutMs ?? 0) | 0;
}

/** Normalize optional creation parts for a native call. */
export function normalizeCreationParts(parts: MessageLike | readonly MessageLike[] | undefined): unknown {
  return normalizeMessageLikePayload(parts ?? []);
}
