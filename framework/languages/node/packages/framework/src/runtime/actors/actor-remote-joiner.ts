import type {
  ActorRef,
  RoutingId,
  ZLinkActor,
  ZLinkMessageSerializer
} from '../../contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException,
  ZLinkSpotKind
} from '../../contracts';
import type { Message } from '../../contracts/Common/Message';
import type { ZLinkBackendSpotNode } from '../backend/contracts';
import type { ZLinkLocationLifecycle } from '../locations';
import { throwIfAborted } from '../abort';
import { routingIdsEqual } from '../routing-id';
import type {
  ZLinkSpotRouteResolver,
  ZLinkSpotRouteTarget
} from '../spots/spot-routing-internal';
import type { ZLinkActorJoinCoordinator, ZLinkActorJoinRuntimeResult } from './actor-runtime-contracts';
import type { ZLinkActorRoutedJoinTransport } from './actor-routed-join-transport';
import type { ZLinkActorSourceTransfer } from './actor-source-transfer';
import { ZLinkActorRuntimeState } from './actor-runtime-state';
import { ZLinkPostCommitActorBinder } from './post-commit-actor-binder';
import { ZLinkPostCommitActorLocation } from './post-commit-actor-location';
import { ZLinkLocalNativeActorJoin } from './actor-local-native-join';
import { ZLinkRemoteTwoPhaseActorJoin } from './actor-remote-two-phase-join';

export interface ZLinkActorNativeJoinCoordinatorOptions {
  readonly node: ZLinkBackendSpotNode | (() => ZLinkBackendSpotNode);
  readonly spotRouteResolver?: ZLinkSpotRouteResolver;
  readonly routedTransport?: ZLinkActorRoutedJoinTransport;
  readonly locationLifecycle?: ZLinkLocationLifecycle;
  readonly remoteActorBinder?: (actorRef: ActorRef, signal?: AbortSignal, force?: boolean) => Promise<void>;
  readonly postCommitErrorReporter?: (error: unknown) => void;
  readonly sourceTransfer?: ZLinkActorSourceTransfer;
  readonly messageSerializers?: ReadonlyMap<string, ZLinkMessageSerializer>;
  readonly shutdownSignal?: AbortSignal;
}

/** Selects local native join or remote two-phase transfer for each destination. */
export class ZLinkActorNativeJoinCoordinator implements ZLinkActorJoinCoordinator {
  private readonly localJoin: ZLinkLocalNativeActorJoin;
  private readonly remoteJoin: ZLinkRemoteTwoPhaseActorJoin;

  constructor(private readonly options: ZLinkActorNativeJoinCoordinatorOptions) {
    const postCommitBinder = options.remoteActorBinder === undefined
      ? undefined
      : new ZLinkPostCommitActorBinder({
          bind: (actorRef, force) => options.remoteActorBinder!(actorRef, undefined, force),
          reportError: options.postCommitErrorReporter,
          signal: options.shutdownSignal
        });
    const postCommitLocation = options.locationLifecycle === undefined
      ? undefined
      : new ZLinkPostCommitActorLocation({
          lifecycle: options.locationLifecycle,
          reportError: options.postCommitErrorReporter,
          signal: options.shutdownSignal
        });
    this.localJoin = new ZLinkLocalNativeActorJoin({
      postCommitLocation,
      postCommitBinder
    });
    this.remoteJoin = new ZLinkRemoteTwoPhaseActorJoin({
      routedTransport: options.routedTransport,
      locationLifecycle: options.locationLifecycle,
      postCommitLocation,
      sourceTransfer: options.sourceTransfer,
      messageSerializers: options.messageSerializers,
      postCommitBinder
    });
  }

  async joinSpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    spotRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinRuntimeResult<Message>> {
    throwIfAborted(signal);
    const node = this.node();
    const actorRef = state.ensureNativeActorRef(node);
    const target = await this.options.spotRouteResolver?.resolve(spotRid, signal);
    if (target !== undefined && !routingIdsEqual(target.targetNodeRid, actorRef.nodeRid)) {
      if (!this.remoteJoin.canJoin(target)) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorRouteNotFound,
          `Remote actor join for '${actor.actorId}' requires the two-phase routed transfer protocol.`
        );
      }
      return await this.remoteJoin.joinSpot(node, actor, state, actorRef, target, request, timeoutMs, signal);
    }
    return await this.localJoin.joinSpot(
      node,
      actor,
      state,
      actorRef,
      spotRid,
      target,
      request,
      timeoutMs,
      signal
    );
  }

  async joinEntrySpot(
    actor: ZLinkActor,
    state: ZLinkActorRuntimeState,
    nodeRid: RoutingId,
    request: Message,
    timeoutMs: number | undefined,
    signal: AbortSignal | undefined
  ): Promise<ZLinkActorJoinRuntimeResult<Message>> {
    throwIfAborted(signal);
    const node = this.node();
    const actorRef = state.ensureNativeActorRef(node);
    const remoteEntry = routingIdsEqual(nodeRid, actorRef.nodeRid)
      ? undefined
      : await this.tryResolveRemoteEntry(nodeRid, signal);
    if (
      remoteEntry !== undefined &&
      !routingIdsEqual(remoteEntry.targetNodeRid, actorRef.nodeRid) &&
      this.remoteJoin.canJoin(remoteEntry)
    ) {
      return await this.remoteJoin.joinEntrySpot(
        node,
        actor,
        state,
        actorRef,
        { ...remoteEntry, spotKind: ZLinkSpotKind.Entry },
        request,
        timeoutMs,
        signal
      );
    }
    return await this.localJoin.joinEntrySpot(node, actor, state, actorRef, nodeRid, request, timeoutMs);
  }

  private async tryResolveRemoteEntry(
    nodeRid: RoutingId,
    signal: AbortSignal | undefined
  ): Promise<ZLinkSpotRouteTarget | undefined> {
    if (this.options.spotRouteResolver === undefined) {
      return undefined;
    }
    try {
      return await this.options.spotRouteResolver.resolve(nodeRid, signal);
    } catch {
      return undefined;
    }
  }

  private node(): ZLinkBackendSpotNode {
    return typeof this.options.node === 'function'
      ? this.options.node()
      : this.options.node;
  }
}
