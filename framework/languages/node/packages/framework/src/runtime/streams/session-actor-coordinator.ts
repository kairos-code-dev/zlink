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
import {
  ZLinkActorSessionBindingRegistry
} from './actor-session-binding-registry';
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
}

export interface ZLinkRemoteBoundSessionBindRelay {
  relayRemoteBoundSessionBind(stream: ZLinkManagedStream, actorRef: ActorRef): void;
}

export class ZLinkSessionActorCoordinator {
  constructor(
    private readonly routes: ZLinkActorSessionBindingRegistry<DefaultZLinkSessionContext, DefaultZLinkSessionActor>,
    private readonly remoteBoundSessions: ZLinkRemoteBoundSessionBindRelay,
    private readonly sessionActorRuntime: ConstructorParameters<typeof DefaultZLinkSessionActor>[0],
    private readonly options: ZLinkSessionActorCoordinatorOptions = {}
  ) {}

  async bind(
    context: DefaultZLinkSessionContext,
    actorOrRef: ZLinkActor | ActorRef,
    signal?: AbortSignal
  ): Promise<DefaultZLinkSessionActor> {
    throwIfAborted(signal);
    const actorRef = isActorRef(actorOrRef)
      ? actorOrRef
      : this.resolveActorRef(actorOrRef);
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

    await this.bindNativeActor(context, actorRef, signal);

    const bindingToken = createBindingToken();
    const sessionActor = new DefaultZLinkSessionActor(this.sessionActorRuntime, actorRef, bindingToken);
    this.routes.bind(context, sessionActor, bindingToken);
    return sessionActor;
  }

  async bindOrGet(
    context: DefaultZLinkSessionContext,
    actorRef: ActorRef,
    signal?: AbortSignal
  ): Promise<DefaultZLinkSessionActor> {
    throwIfAborted(signal);
    const existing = this.routes.find(actorRef.actorId);
    if (existing !== undefined && sameActorRef(existing.ref, actorRef)) {
      if (context.findBoundActor(actorRef.actorId) === existing) {
        return existing;
      }
      this.routes.unbindActor(actorRef.actorId);
      return await this.bind(context, actorRef, signal);
    }
    if (existing !== undefined) {
      try {
        await this.rebindActor(actorRef, signal);
      } catch (error) {
        throw new ZLinkFrameworkException(
          ZLinkFrameworkErrorKind.ActorLocationStale,
          `Actor '${actorRef.actorId}' bound session ref is stale and could not be rebound.`,
          true,
          error
        );
      }
      this.routes.unbindActor(actorRef.actorId);
    }
    return await this.bind(context, actorRef, signal);
  }

  async rebindActor(actorRef: ActorRef, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    const route = this.routes.route(actorRef.actorId);
    if (route === undefined) {
      return;
    }
    if (sameActorRef(route.actor.ref, actorRef)) {
      return;
    }
    await this.bindNativeActor(route.context, actorRef, signal);
    this.relayRemoteBoundSessionBind(route.context, actorRef);
  }

  async refreshActor(actorRef: ActorRef, signal?: AbortSignal): Promise<void> {
    throwIfAborted(signal);
    const route = this.routes.route(actorRef.actorId);
    if (route === undefined) {
      return;
    }
    await this.bindNativeActor(route.context, actorRef, signal);
    this.relayRemoteBoundSessionBind(route.context, actorRef);
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
      `Actor '${actor.actorId}' does not have a concrete actor ref.`
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
    const sessionRid = context.routingId;
    if (sessionRid === undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorRouteNotFound,
        'Actor session binding requires a stream routing id.'
      );
    }
    await context.stream.bindActor(actorRef, this.options.actorBindTimeoutMs ?? 2000, signal);
  }

  private relayRemoteBoundSessionBind(
    context: DefaultZLinkSessionContext,
    actorRef: ActorRef
  ): void {
    if (!(context.stream instanceof ZLinkManagedStream)) {
      return;
    }
    this.remoteBoundSessions.relayRemoteBoundSessionBind(context.stream, actorRef);
  }
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
