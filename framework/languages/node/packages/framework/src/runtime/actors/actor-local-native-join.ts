import { randomUUID } from 'node:crypto';
import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkMessageSerializer,
} from '../../contracts';
import type { ZLinkActorJoinRuntimeResult } from './actor-runtime-contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import type { ZLinkBackendMeshNode } from '../backend/contracts';
import {
  closeMeshCompletion,
  type ZLinkMeshCompletionTable
} from '../backend/mesh-completion-table';
import { createAbortError, throwIfAborted } from '../abort';
import type { ZLinkSpotRouteTarget } from '../spots/spot-routing-internal';
import {
  ZLinkActorRuntimeState,
  toFrameworkActorRef,
  toFrameworkRoutingId
} from './actor-runtime-state';
import type { ZLinkPostCommitActorBinder } from './post-commit-actor-binder';
import type { ZLinkPostCommitActorLocation } from './post-commit-actor-location';
import { toBindingRoutingId } from '../routing-id';
import { routingIdsEqual } from '../routing-id';
import type {
  ZLinkActorSourceTransfer,
  ZLinkPreparedActorSource
} from './actor-source-transfer';
import {
  buildRemoteActorJoinRequestPayload,
  REMOTE_ACTOR_JOIN_COMMIT,
  ZLINK_REMOTE_ACTOR_SOURCE_LEAVE_TERMINAL
} from './actor-remote-wire';

export interface ZLinkLocalNativeActorJoinOptions {
  readonly postCommitLocation?: ZLinkPostCommitActorLocation;
  readonly postCommitBinder?: ZLinkPostCommitActorBinder;
  readonly completionTableProvider: () => ZLinkMeshCompletionTable | undefined;
  readonly sourceTransfer?: ZLinkActorSourceTransfer;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly remoteActivationWaiter?: (
    actorId: string,
    targetNodeRid: RoutingId,
    timeoutMs: number | undefined,
    signal?: AbortSignal
  ) => Promise<ActorRef | undefined>;
}

/** Owns core-native actor joins and their local runtime state transition. */
export class ZLinkLocalNativeActorJoin {
  constructor(private readonly options: ZLinkLocalNativeActorJoinOptions) {}

  async joinSpot(
    node: ZLinkBackendMeshNode,
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    actorRef: ActorRef,
    spotId: RoutingId,
    spotRouteTarget: ZLinkSpotRouteTarget | undefined,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinRuntimeResult<Message>> {
    if (signal?.aborted === true) throw createAbortError();
    const completions = this.requireCompletions();
    const target = requireUserSpotRoute(spotRouteTarget, spotId);
    const remote = !routingIdsEqual(
      toFrameworkRoutingId(node.status().routingId),
      target.targetNodeRid
    );
    let prepared: ZLinkPreparedActorSource | undefined;
    let transferId: string | undefined;
    let requestPayload = Buffer.from(request.data());
    if (remote) {
      const actorType = state.actorType;
      if (actorType === undefined || this.options.sourceTransfer === undefined) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorRouteNotFound,
          `Actor '${actor.actorId}' remote transfer state is not configured.`
        );
      }
      prepared = await this.options.sourceTransfer.prepareSource(actor, state, signal, 'core');
      transferId = randomUUID();
      requestPayload = Buffer.from(JSON.stringify(buildRemoteActorJoinRequestPayload({
        actorId: actor.actorId,
        actorType,
        actorRef: actorRef as never,
        expectedMembershipEpoch: state.spotMembershipEpoch,
        actorEntryNodeRid: state.entryNodeRid ?? actorRef.nodeRid as unknown as RoutingId,
        actorCreateRequest: state.createRequestPayload,
        request,
        targetSpotId: target.spotId,
        routerChannelId: target.routerChannelId,
        sourceSpotId: state.spotId ?? toFrameworkRoutingId(node.entrySpot().routingId),
        boundSessionTarget: state.remoteBoundSessionTarget,
        phase: REMOTE_ACTOR_JOIN_COMMIT,
        transferId,
        transferAdapterKey: prepared.adapterKey,
        transferState: Buffer.from(
          prepared.state.toEncodedPayload(this.options.messageSerializers).data()
        ),
        handoffBacklog: prepared.handoffBacklog
      })));
    }
    let completion;
    try {
      const operationId = await submitJoinWhenConnected(
        () => node.joinActorSpot(
          normalizeNativeActorRef(actorRef),
          toBindingRoutingId(target.targetNodeRid),
          toBindingRoutingId(target.spotId),
          target.targetSpotGeneration,
          requestPayload,
          timeoutMs
        ),
        timeoutMs,
        signal
      );
      completion = await completions.wait(operationId, signal);
    } catch (error) {
      await prepared?.rollback();
      throw error;
    }
    const control = completion.kindData;
    if (
      control?.kind !== 'actorJoinCompletion' ||
      control.actor === null
    ) {
      await prepared?.rollback();
      closeMeshCompletion(completion);
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor join failed for '${actor.actorId}' with result '${completion.terminalResult}' and errno '${completion.failureErrno}'.`
      );
    }
    if (control.joinResult !== 0) {
      try {
        await prepared?.rollback();
        return {
          accepted: false,
          actor: toFrameworkActorRef(control.actor as never),
          reply: completion.parts[0]
        };
      } finally {
        disposeParts(completion.parts.slice(1));
      }
    }
    if (completion.terminalResult !== 0 || completion.failureErrno !== 0) {
      await prepared?.rollback();
      closeMeshCompletion(completion);
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor join failed for '${actor.actorId}' with result '${completion.terminalResult}' and errno '${completion.failureErrno}'.`
      );
    }

    state.setNativeActorRef(control.actor as never);
    state.setJoinedSpot(
      toFrameworkRoutingId(control.location.spotId ?? target.spotId),
      undefined,
      control.location.membershipEpoch
    );
    state.setRemoteActorPacketTarget(undefined);
    if (remote) {
      try {
        await prepared?.sourceLeaveCompletion;
      } catch (error) {
        await this.publishSourceLeaveTerminal(
          node,
          completions,
          target.targetNodeRid,
          actor.actorId,
          requireTransferId(transferId),
          false,
          timeoutMs,
          signal
        );
        prepared?.commit(target, toFrameworkActorRef(control.actor as never), []);
        throw error;
      }
      await this.publishSourceLeaveTerminal(
        node,
        completions,
        target.targetNodeRid,
        actor.actorId,
        requireTransferId(transferId),
        true,
        timeoutMs,
        signal
      );
      const targetActorRef = await this.options.remoteActivationWaiter?.(
        actor.actorId,
        target.targetNodeRid,
        timeoutMs,
        signal
      );
      prepared?.commit(
        target,
        targetActorRef ?? toFrameworkActorRef(control.actor as never),
        []
      );
    } else {
      prepared?.commit(target, toFrameworkActorRef(control.actor as never), []);
    }
    if (!remote && state.actorType !== undefined) {
      this.options.postCommitLocation?.joinedEventually(
        state.actorType,
        actor.actorId,
        spotRouteTarget?.routerChannelId ?? '',
        toFrameworkRoutingId(control.location.spotId ?? target.spotId),
        control.location.spotGeneration,
        control.location.membershipEpoch,
        node.status().lifecycleGeneration
      );
    }
    await this.options.postCommitBinder?.bind(toFrameworkActorRef(control.actor as never));
    try {
      return {
        accepted: true,
        actor: toFrameworkActorRef(control.actor as never),
        reply: completion.parts[0]
      };
    } finally {
      disposeParts(completion.parts.slice(1));
    }
  }

  async joinEntrySpot(
    node: ZLinkBackendMeshNode,
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    actorRef: ActorRef,
    nodeRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined
  ): Promise<ZLinkActorJoinRuntimeResult<Message>> {
    const completions = this.requireCompletions();
    if (state.spotId !== undefined) {
      const leaveOperationId = node.leaveActor(
        normalizeNativeActorRef(actorRef),
        state.spotMembershipEpoch,
        timeoutMs
      );
      const leaveCompletion = await completions.wait(leaveOperationId);
      if (leaveCompletion.terminalResult !== 0 || leaveCompletion.failureErrno !== 0) {
        closeMeshCompletion(leaveCompletion);
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorRouteNotFound,
          `Actor SPOT leave failed for '${actor.actorId}' with result '${leaveCompletion.terminalResult}' and errno '${leaveCompletion.failureErrno}'.`
        );
      }
      closeMeshCompletion(leaveCompletion);
    }
    const operationId = node.joinActorEntrySpot(
      normalizeNativeActorRef(actorRef),
      toBindingRoutingId(nodeRid),
      Buffer.from(request.data()),
      timeoutMs
    );
    const completion = await completions.wait(operationId);
    const control = completion.kindData;
    if (
      completion.terminalResult !== 0 ||
      completion.failureErrno !== 0 ||
      control?.kind !== 'actorJoinCompletion' ||
      control.joinResult !== 0 ||
      control.actor === null
    ) {
      closeMeshCompletion(completion);
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        `Actor entry SPOT join failed for '${actor.actorId}' with result '${completion.terminalResult}' and errno '${completion.failureErrno}'.`
      );
    }

    state.setNativeActorRef(control.actor as never);
    state.clearJoinedSpot();
    if (state.actorType !== undefined) {
      this.options.postCommitLocation?.leftEventually(
        state.actorType,
        actor.actorId,
        toFrameworkRoutingId(control.location.spotId ?? nodeRid),
        control.location.spotGeneration,
        control.location.membershipEpoch,
        node.status().lifecycleGeneration
      );
    }
    try {
      return {
        accepted: true,
        actor: toFrameworkActorRef(control.actor as never),
        reply: completion.parts[0]
      };
    } finally {
      disposeParts(completion.parts.slice(1));
    }
  }

  private requireCompletions(): ZLinkMeshCompletionTable {
    const completions = this.options.completionTableProvider();
    if (completions === undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        'Actor join runtime is not started.'
      );
    }
    return completions;
  }

  private async publishSourceLeaveTerminal(
    node: ZLinkBackendMeshNode,
    completions: ZLinkMeshCompletionTable,
    targetNodeRid: RoutingId,
    actorId: string,
    transferId: string,
    succeeded: boolean,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<void> {
    const operationId = node.requestToNode(
      toBindingRoutingId(targetNodeRid),
      Buffer.from(JSON.stringify({
        packetName: ZLINK_REMOTE_ACTOR_SOURCE_LEAVE_TERMINAL,
        transferId,
        actorId,
        succeeded
      })),
      { timeoutMs }
    );
    const completion = await completions.wait(operationId, signal);
    try {
      if (completion.terminalResult !== 0 || completion.failureErrno !== 0) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorRouteNotFound,
          `Actor '${actorId}' source leave terminal result was not acknowledged.`
        );
      }
    } finally {
      closeMeshCompletion(completion);
    }
  }
}

function normalizeNativeActorRef(actor: ActorRef): ActorRef {
  return {
    nodeRid: actor.nodeRid,
    actorId: actor.actorId,
    generation: actor.generation
  };
}

function requireTransferId(transferId: string | undefined): string {
  if (transferId === undefined) {
    throw new Error('Remote actor join has no private transfer id.');
  }
  return transferId;
}

async function submitJoinWhenConnected<T>(
  submit: () => T,
  timeoutMs: number | undefined,
  signal: AbortSignal | undefined
): Promise<T> {
  const deadline = Date.now() + Math.min(timeoutMs ?? 5_000, 5_000);
  for (;;) {
    throwIfAborted(signal);
    try {
      return submit();
    } catch (error) {
      if (
        !(error instanceof Error) ||
        !error.message.includes('Transport endpoint is not connected') ||
        Date.now() >= deadline
      ) {
        throw error;
      }
      await new Promise<void>((resolve) => setTimeout(resolve, 10));
    }
  }
}

function requireUserSpotRoute(
  target: ZLinkSpotRouteTarget | undefined,
  spotId: RoutingId
): ZLinkSpotRouteTarget & { readonly targetSpotGeneration: bigint } {
  if (target?.targetSpotGeneration === undefined || target.targetSpotGeneration <= 0n) {
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.ActorRouteNotFound,
      `SPOT '${spotId}' has no valid Core lifecycle generation.`
    );
  }
  return target as ZLinkSpotRouteTarget & { readonly targetSpotGeneration: bigint };
}

function disposeParts(parts: readonly Message[]): void {
  for (const part of parts) {
    part.close();
  }
}
