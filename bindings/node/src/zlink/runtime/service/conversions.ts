// SPDX-License-Identifier: MPL-2.0

import { RoutingId } from '../../contracts/core';
import type { MessageLike } from '../../contracts/messaging';
import type { ActorLocation, ActorRef } from '../../contracts/service';
import type { ActorLocationRaw, ActorRefRaw } from '../native/binding_service_types';
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
