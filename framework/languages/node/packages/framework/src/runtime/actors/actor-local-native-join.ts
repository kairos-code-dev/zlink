import type {
  RoutingId,
  ZLinkActor,
} from '../../contracts';
import type { ZLinkActorJoinRuntimeResult } from './actor-runtime-contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import type {
  ZLinkBackendActorJoinEntrySpotResult,
  ZLinkBackendActorJoinResult,
  ZLinkBackendActorRef,
  ZLinkBackendSpotNode
} from '../backend/contracts';
import { createAbortError } from '../abort';
import type { ZLinkSpotRouteTarget } from '../spots/spot-routing-internal';
import {
  ZLinkActorRuntimeState,
  toFrameworkActorRef,
  toFrameworkRoutingId
} from './actor-runtime-state';
import type { ZLinkPostCommitActorBinder } from './post-commit-actor-binder';
import type { ZLinkPostCommitActorLocation } from './post-commit-actor-location';

export interface ZLinkLocalNativeActorJoinOptions {
  readonly postCommitLocation?: ZLinkPostCommitActorLocation;
  readonly postCommitBinder?: ZLinkPostCommitActorBinder;
}

/** Owns core-native actor joins and their local runtime state transition. */
export class ZLinkLocalNativeActorJoin {
  constructor(private readonly options: ZLinkLocalNativeActorJoinOptions) {}

  async joinSpot(
    node: ZLinkBackendSpotNode,
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    actorRef: ZLinkBackendActorRef,
    spotRid: RoutingId,
    spotRouteTarget: ZLinkSpotRouteTarget | undefined,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinRuntimeResult<Message>> {
    const { result, parts } = await new Promise<{
      result: ZLinkBackendActorJoinResult;
      parts: readonly Message[];
    }>((resolve, reject) => {
      if (signal?.aborted === true) {
        reject(createAbortError());
        return;
      }
      const submitted = node.joinActor(
        actorRef,
        spotRouteTarget?.targetNodeRid ?? actorRef.nodeRid,
        toBackendRoutingId(spotRid),
        request,
        (joinResult: ZLinkBackendActorJoinResult, replyParts: readonly Message[]) =>
          resolve({ result: joinResult, parts: replyParts }),
        timeoutMs
      );
      if (!submitted) {
        reject(new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorRouteNotFound,
          `Actor join submit failed for '${actor.actorId}'.`
        ));
      }
    });

    if (result.result !== 0) {
      disposeParts(parts);
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor join failed for '${actor.actorId}' with '${result.result}'.`
      );
    }

    state.setNativeActorRef(result.actor);
    state.setJoinedSpot(toFrameworkRoutingId(result.joinedSpotRid));
    state.setRemoteActorPacketTarget(undefined);
    if (state.actorType !== undefined) {
      this.options.postCommitLocation?.joinedEventually(
        state.actorType,
        actor.actorId,
        spotRouteTarget?.routerChannelId ?? '',
        toFrameworkRoutingId(result.joinedSpotRid)
      );
    }
    await this.options.postCommitBinder?.bind(toFrameworkActorRef(result.actor));
    try {
      return {
        accepted: true,
        actor: toFrameworkActorRef(result.actor),
        reply: parts[0]
      };
    } finally {
      disposeParts(parts.slice(1));
    }
  }

  async joinEntrySpot(
    node: ZLinkBackendSpotNode,
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    actorRef: ZLinkBackendActorRef,
    nodeRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined
  ): Promise<ZLinkActorJoinRuntimeResult<Message>> {
    const { result, parts } = await new Promise<{
      result: ZLinkBackendActorJoinEntrySpotResult;
      parts: readonly Message[];
    }>((resolve, reject) => {
      const submitted = node.joinActorEntrySpot(
        actorRef,
        toBackendRoutingId(nodeRid),
        request,
        (entryResult, replyParts) => resolve({ result: entryResult, parts: replyParts }),
        timeoutMs
      );
      if (!submitted) {
        reject(new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorRouteNotFound,
          `Actor entry SPOT join submit failed for '${actor.actorId}'.`
        ));
      }
    });

    if (result.result !== 0) {
      disposeParts(parts);
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor entry SPOT join failed for '${actor.actorId}' with '${result.result}'.`
      );
    }

    if (result.joinResultCode === 0) {
      state.setNativeActorRef(result.actor);
      state.clearJoinedSpot();
      if (state.actorType !== undefined) {
        this.options.postCommitLocation?.leftEventually(state.actorType, actor.actorId);
      }
    }
    try {
      return {
        accepted: true,
        actor: toFrameworkActorRef(result.actor),
        reply: parts[0]
      };
    } finally {
      disposeParts(parts.slice(1));
    }
  }
}

function disposeParts(parts: readonly Message[]): void {
  for (const part of parts) {
    part.close();
  }
}

function toBackendRoutingId(routingId: RoutingId): ZLinkBackendActorRef['nodeRid'] {
  return routingId as unknown as ZLinkBackendActorRef['nodeRid'];
}
