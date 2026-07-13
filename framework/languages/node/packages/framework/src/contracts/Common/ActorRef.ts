import type { RoutingId } from './CoreTypes';

export interface ActorRef {
  readonly nodeRid: RoutingId;
  readonly actorId: string;
  readonly generation: bigint;
}

export interface ZLinkActorRefSnapshot {
  readonly nodeRid: RoutingId;
  readonly actorId: string;
  readonly generation: bigint;
}

export function zlinkActorRefSnapshotFrom(actorRef: ActorRef): ZLinkActorRefSnapshot {
  return {
    nodeRid: actorRef.nodeRid,
    actorId: actorRef.actorId,
    generation: actorRef.generation
  };
}

export function zlinkActorRefSnapshotToActorRef(snapshot: ZLinkActorRefSnapshot): ActorRef {
  return {
    nodeRid: snapshot.nodeRid,
    actorId: snapshot.actorId,
    // JSON transports carry uint64 values as decimal strings. Normalize the
    // wire value at the public conversion boundary so callers keep bigint.
    generation: BigInt(snapshot.generation)
  };
}
