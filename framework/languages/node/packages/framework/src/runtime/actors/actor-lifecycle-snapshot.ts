import type {
  ActorRef,
  ZLinkActor,
  ZLinkActorJoinRequest,
  ZLinkActorMembership
} from '../../contracts';
import { ZLinkConfigurationException } from '../configuration';

export const ZLINK_ACTOR_LIFECYCLE_SNAPSHOT = Symbol('zlink.actor.lifecycle-snapshot');

export interface ZLinkActorLifecycleSnapshotSource {
  readonly actorRef: ActorRef;
  readonly actorType: string;
  readonly membershipEpoch: bigint;
}

interface ZLinkActorLifecycleSnapshotContext {
  readonly actorId?: string;
  readonly [ZLINK_ACTOR_LIFECYCLE_SNAPSHOT]?: () => ZLinkActorLifecycleSnapshotSource;
}

export function createActorJoinRequest(
  actor: ZLinkActor,
  actorRef?: ActorRef,
  expectedMembershipEpoch?: bigint
): ZLinkActorJoinRequest {
  const source = lifecycleSource(actor);
  return Object.freeze({
    actor: immutableActorRef(actorRef ?? source.actorRef),
    actorType: source.actorType,
    expectedMembershipEpoch: expectedMembershipEpoch ?? source.membershipEpoch
  });
}

export function createActorMembership(
  actor: ZLinkActor,
  actorRef?: ActorRef,
  membershipEpoch?: bigint
): ZLinkActorMembership {
  const source = lifecycleSource(actor);
  return Object.freeze({
    actor: immutableActorRef(actorRef ?? source.actorRef),
    actorType: source.actorType,
    membershipEpoch: membershipEpoch ?? source.membershipEpoch
  });
}

function lifecycleSource(actor: ZLinkActor): ZLinkActorLifecycleSnapshotSource {
  const rawContext: unknown = actor.context;
  const context = typeof rawContext === 'object' && rawContext !== null
    ? (rawContext as ZLinkActorLifecycleSnapshotContext)
    : undefined;
  // A caller can hand in a value that carries no Framework context at all, so the
  // failure path reports the configuration error instead of dereferencing it.
  const actorId = context?.actorId ?? '<unknown>';
  const snapshot = context?.[ZLINK_ACTOR_LIFECYCLE_SNAPSHOT];
  const source = snapshot === undefined ? undefined : snapshot.call(context);
  if (source === undefined) {
    throw new ZLinkConfigurationException(
      `Actor '${actorId}' does not expose a framework lifecycle identity snapshot.`
    );
  }
  if (source.actorRef.actorId !== actorId || source.actorType.length === 0) {
    throw new ZLinkConfigurationException(`Actor '${actorId}' lifecycle identity is invalid.`);
  }
  return source;
}

function immutableActorRef(actorRef: ActorRef): ActorRef {
  return Object.freeze({
    nodeRid: actorRef.nodeRid,
    actorId: actorRef.actorId,
    generation: actorRef.generation
  });
}
