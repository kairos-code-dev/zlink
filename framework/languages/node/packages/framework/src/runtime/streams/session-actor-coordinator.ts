import type {
  ActorRef,
  ZLinkActor
} from '../../contracts';
import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';
import { throwIfAborted } from '../abort';
import { routingIdsEqual } from '../routing-id';
import type { ZLinkRuntimeMetrics } from '../diagnostics';
import {
  ZLinkActorSessionBindingRegistry
} from './actor-session-binding-registry';
import { ZLinkActorSessionLifecycleCoordinator } from './actor-session-lifecycle-coordinator';
import {
  ZLinkManagedStream
} from './managed-stream';
import {
  DefaultZLinkSessionActor,
  DefaultZLinkSessionContext
} from './session-context';

export interface ZLinkSessionActorCoordinatorOptions {
  readonly actorBindTimeoutMs?: number;
  readonly actorRefResolver?: (actor: ZLinkActor) => ActorRef;
  readonly nativeActorNodeProvider?: () => {
    status(): { readonly routingId: unknown };
  } | undefined;
  readonly confirmRemoteActorSessionBinding?: (
    actor: ActorRef,
    sessionRid: ActorRef['nodeRid'],
    signal?: AbortSignal
  ) => Promise<void>;
  readonly metrics?: ZLinkRuntimeMetrics;
}

export interface ZLinkRemoteBoundSessionBindRelay {
  relayRemoteBoundSessionBind(stream: ZLinkManagedStream, actorRef: ActorRef): void;
}

export class ZLinkSessionActorCoordinator {
  constructor(
    private readonly routes: ZLinkActorSessionBindingRegistry<DefaultZLinkSessionContext, DefaultZLinkSessionActor>,
    private readonly remoteBoundSessions: ZLinkRemoteBoundSessionBindRelay,
    private readonly sessionActorRuntime: ConstructorParameters<typeof DefaultZLinkSessionActor>[0],
    private readonly options: ZLinkSessionActorCoordinatorOptions = {},
    private readonly lifecycle = new ZLinkActorSessionLifecycleCoordinator()
  ) {}

  async bind(
    context: DefaultZLinkSessionContext,
    actorOrRef: ZLinkActor | ActorRef,
    signal?: AbortSignal
  ): Promise<DefaultZLinkSessionActor> {
    const actorRef = isActorRef(actorOrRef)
      ? actorOrRef
      : this.resolveActorRef(actorOrRef);
    return await this.lifecycle.run(actorRef.actorId, async () => this.replaceBinding(context, actorRef, signal));
  }

  private async replaceBinding(
    context: DefaultZLinkSessionContext,
    actorRef: ActorRef,
    signal?: AbortSignal
  ): Promise<DefaultZLinkSessionActor> {
    const bindStartedAt = process.hrtime.bigint();
    try {
      return await this.replaceBindingCore(context, actorRef, signal);
    } finally {
      this.options.metrics?.duration(
        'zlink.stream.session.bind.duration',
        Number(process.hrtime.bigint() - bindStartedAt) / 1e9
      );
    }
  }

  private async replaceBindingCore(
    context: DefaultZLinkSessionContext,
    actorRef: ActorRef,
    signal?: AbortSignal
  ): Promise<DefaultZLinkSessionActor> {
    throwIfAborted(signal);
    if (actorRef.actorId.trim().length === 0) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        'Actor id must not be empty.'
      );
    }
    if (context.routingId === undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        'Actor session binding requires a stream routing id.'
      );
    }

    const previous = this.routes.route(actorRef.actorId);
    const sameIncarnation =
      previous !== undefined
      && previous.actor.ref.actorId === actorRef.actorId
      && BigInt(previous.actor.ref.generation) === BigInt(actorRef.generation);
    const reuseActor =
      previous?.context === context && sameIncarnation ? previous.actor : undefined;
    const previousRef = previous?.actor.ref;
    const replacesSameNativeBinding = previous?.context === context && sameIncarnation;
    if (previous !== undefined && !replacesSameNativeBinding) {
      await this.unbindNativeActor(previous.context, actorRef.actorId, signal);
    }
    let replacementBound = false;
    try {
      await this.bindNativeActor(context, actorRef, signal);
      replacementBound = true;
      if (this.options.confirmRemoteActorSessionBinding !== undefined) {
        await this.options.confirmRemoteActorSessionBinding(
          actorRef,
          this.actorBindingRoutingId(context),
          signal
        );
      }
    } catch (error) {
      const rollbackErrors: unknown[] = [];
      if (replacementBound && !replacesSameNativeBinding) {
        await this.unbindNativeActor(context, actorRef.actorId).catch((rollbackError) => {
          rollbackErrors.push(rollbackError);
        });
      }
      if (
        previous !== undefined
        && previousRef !== undefined
      ) {
        try {
          await this.bindNativeActor(previous.context, previousRef);
          try {
            if (
              this.options.confirmRemoteActorSessionBinding !== undefined
              && previous.context.routingId !== undefined
            ) {
              await this.options.confirmRemoteActorSessionBinding(
                previousRef,
                this.actorBindingRoutingId(previous.context)
              );
            } else {
              this.relayRemoteBoundSessionBind(previous.context, previousRef);
            }
          } catch (relayError) {
            if (!replacesSameNativeBinding) {
              await this.unbindNativeActor(previous.context, previousRef.actorId).catch((unbindError) => {
                rollbackErrors.push(unbindError);
              });
            }
            throw relayError;
          }
        } catch (rollbackError) {
          rollbackErrors.push(rollbackError);
        }
      }
      if (rollbackErrors.length > 0) {
        throw new AggregateError(
          [error, ...rollbackErrors],
          `Actor '${actorRef.actorId}' session bind and rollback failed.`
        );
      }
      throw error;
    }

    const bindingToken = reuseActor?.bindingToken ?? createBindingToken();
    const boundActorRef = withBindingGeneration(
      actorRef,
      context.stream instanceof ZLinkManagedStream
        ? context.stream.actorBindingGeneration(actorRef.actorId)
        : undefined,
      previous?.actor.ref
    );
    const sessionActor = reuseActor ?? new DefaultZLinkSessionActor(this.sessionActorRuntime, boundActorRef, bindingToken);
    sessionActor.updateRef(boundActorRef);
    if (previous === undefined) {
      this.routes.bind(context, sessionActor, bindingToken);
    } else {
      this.routes.replace(previous, context, sessionActor, bindingToken);
    }
    return sessionActor;
  }

  async bindOrGet(
    context: DefaultZLinkSessionContext,
    actorRef: ActorRef,
    signal?: AbortSignal
  ): Promise<DefaultZLinkSessionActor> {
    return await this.lifecycle.run(actorRef.actorId, async () => {
      throwIfAborted(signal);
      const existing = this.routes.find(actorRef.actorId);
      if (existing !== undefined && sameActorRef(existing.ref, actorRef)) {
        if (context.findBoundActor(actorRef.actorId) === existing) {
          return existing;
        }
        return await this.replaceBinding(context, actorRef, signal);
      }
      if (existing !== undefined) {
        try {
          return await this.replaceBinding(context, actorRef, signal);
        } catch (error) {
          throw new ZLinkFrameworkException(
            ZLinkFrameworkErrorKind.ActorLocationStale,
            `Actor '${actorRef.actorId}' bound session ref is stale and could not be rebound.`,
            true,
            error
          );
        }
      }
      return await this.replaceBinding(context, actorRef, signal);
    });
  }

  async rebindActor(actorRef: ActorRef, signal?: AbortSignal): Promise<void> {
    await this.lifecycle.run(actorRef.actorId, async () => {
      throwIfAborted(signal);
      const route = this.routes.route(actorRef.actorId);
      if (route === undefined || sameActorRef(route.actor.ref, actorRef)) return;
      requireSameIncarnation(route.actor.ref, actorRef);
      await this.replaceBinding(route.context, actorRef, signal);
    });
  }

  async refreshActor(actorRef: ActorRef, signal?: AbortSignal): Promise<void> {
    await this.lifecycle.run(actorRef.actorId, async () => {
      throwIfAborted(signal);
      const route = this.routes.route(actorRef.actorId);
      if (route === undefined) return;
      requireSameIncarnation(route.actor.ref, actorRef);
      await this.replaceBinding(route.context, actorRef, signal);
    });
  }

  async commitActorRoute(actorRef: ActorRef, signal?: AbortSignal): Promise<void> {
    await this.lifecycle.run(actorRef.actorId, async () => {
      throwIfAborted(signal);
      const route = this.routes.route(actorRef.actorId);
      if (route === undefined) return;
      requireSameIncarnation(route.actor.ref, actorRef);
      if (routingIdsEqual(route.actor.ref.nodeRid, actorRef.nodeRid)) {
        route.actor.updateRef(actorRef);
        return;
      }
      await this.replaceBinding(route.context, actorRef, signal);
    });
  }

  private resolveActorRef(actor: ZLinkActor): ActorRef {
    if (this.options.actorRefResolver !== undefined) {
      return this.options.actorRefResolver(actor);
    }
    const state = actor.context as unknown as { actorRef?: ActorRef };
    if (state.actorRef !== undefined) {
      return state.actorRef;
    }
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.ActorRouteNotFound,
      `Actor '${actor.context.actorId}' does not have a concrete actor ref.`
    );
  }

  private async bindNativeActor(
    context: DefaultZLinkSessionContext,
    actorRef: ActorRef,
    signal?: AbortSignal
  ): Promise<void> {
    if (!(context.stream instanceof ZLinkManagedStream)) {
      return;
    }
    try {
      await context.stream.bindActor(actorRef, this.options.actorBindTimeoutMs ?? 2000, signal);
    } catch (error) {
      throw new Error(
        `Actor '${actorRef.actorId}' native session bind failed: ${error instanceof Error ? error.message : String(error)}`,
        { cause: error }
      );
    }
  }

  private actorBindingRoutingId(context: DefaultZLinkSessionContext): ActorRef['nodeRid'] {
    return context.stream instanceof ZLinkManagedStream
      ? context.stream.actorBindingRoutingId
      : context.routingId as ActorRef['nodeRid'];
  }

  private async unbindNativeActor(
    context: DefaultZLinkSessionContext,
    actorId: string,
    signal?: AbortSignal
  ): Promise<void> {
    if (!(context.stream instanceof ZLinkManagedStream)) {
      return;
    }
    try {
      await context.stream.unbindActor(actorId, this.options.actorBindTimeoutMs ?? 2000, signal);
    } catch (error) {
      throw new Error(
        `Actor '${actorId}' previous native session unbind failed: ${error instanceof Error ? error.message : String(error)}`,
        { cause: error }
      );
    }
  }

  private relayRemoteBoundSessionBind(
    context: DefaultZLinkSessionContext,
    actorRef: ActorRef
  ): void {
    if (!(context.stream instanceof ZLinkManagedStream)) {
      return;
    }
    const localNode = this.options.nativeActorNodeProvider?.();
    if (localNode !== undefined && routingIdsEqual(String(localNode.status().routingId), actorRef.nodeRid)) {
      return;
    }
    this.remoteBoundSessions.relayRemoteBoundSessionBind(context.stream, actorRef);
  }
}

function withBindingGeneration(
  actorRef: ActorRef,
  nativeBindingGeneration: bigint | undefined,
  previousRef: ActorRef | undefined
): ActorRef {
  const input = actorRef as ActorRef & { readonly bindingGeneration?: bigint };
  const previous = previousRef as (ActorRef & { readonly bindingGeneration?: bigint }) | undefined;
  const bindingGeneration = input.bindingGeneration
    ?? previous?.bindingGeneration
    ?? nativeBindingGeneration;
  return bindingGeneration === undefined
    ? actorRef
    : { ...actorRef, bindingGeneration } as ActorRef;
}

function isActorRef(value: ZLinkActor | ActorRef): value is ActorRef {
  return (
    typeof value === 'object'
    && 'nodeRid' in value
    && 'generation' in value
    && 'actorId' in value
  );
}

function createBindingToken(): string {
  return `${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}`;
}

function sameActorRef(left: ActorRef, right: ActorRef): boolean {
  return routingIdsEqual(left.nodeRid, right.nodeRid)
    && left.actorId === right.actorId
    && BigInt(left.generation) === BigInt(right.generation);
}

function requireSameIncarnation(current: ActorRef, updated: ActorRef): void {
  if (
    current.actorId === updated.actorId
    && BigInt(current.generation) === BigInt(updated.generation)
  ) {
    return;
  }
  throw new ZLinkFrameworkException(
    ZLinkFrameworkErrorKind.ActorLocationStale,
    `Actor '${updated.actorId}' route update cannot replace object generation `
      + `${String(current.generation)} with ${String(updated.generation)}.`,
    true
  );
}
