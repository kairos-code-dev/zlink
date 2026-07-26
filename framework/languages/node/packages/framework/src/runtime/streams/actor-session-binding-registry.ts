import {
  ZLinkFrameworkErrorKind,
  ZLinkFrameworkException
} from '../../contracts';

export interface ZLinkActorSessionBindingActor {
  readonly actorId: string;
}

export interface ZLinkActorSessionBindingContext<TActor extends ZLinkActorSessionBindingActor> {
  bindLocal(actor: TActor, bindingToken: string): void;
  unbindLocal(actorId: string, bindingToken: string): void;
}

export interface ZLinkActorSessionRoute<
  TContext extends ZLinkActorSessionBindingContext<TActor>,
  TActor extends ZLinkActorSessionBindingActor
> {
  readonly context: TContext;
  readonly actor: TActor;
  readonly bindingToken: string;
  acceptedHighWater: bigint;
  sealId?: string;
}

export class ZLinkActorSessionBindingRegistry<
  TContext extends ZLinkActorSessionBindingContext<TActor>,
  TActor extends ZLinkActorSessionBindingActor
> {
  private readonly routes = new Map<string, ZLinkActorSessionRoute<TContext, TActor>>();

  bind(context: TContext, actor: TActor, bindingToken: string): void {
    this.routes.set(actor.actorId, {
      context,
      actor,
      bindingToken,
      acceptedHighWater: actorAcceptedHighWater(actor)
    });
    context.bindLocal(actor, bindingToken);
  }

  replace(
    previous: ZLinkActorSessionRoute<TContext, TActor>,
    context: TContext,
    actor: TActor,
    bindingToken: string
  ): void {
    const current = this.routes.get(actor.actorId);
    if (current !== previous) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorSessionNotBound,
        `Actor '${actor.actorId}' session binding changed before route replacement.`,
        true
      );
    }

    context.bindLocal(actor, bindingToken);
    const sameLocalBinding = previous.context === context
      && previous.bindingToken === bindingToken;
    if (!sameLocalBinding) {
      try {
        previous.context.unbindLocal(actor.actorId, previous.bindingToken);
      } catch (error) {
        context.unbindLocal(actor.actorId, bindingToken);
        previous.context.bindLocal(previous.actor, previous.bindingToken);
        throw error;
      }
    }
    this.routes.set(actor.actorId, {
      context,
      actor,
      bindingToken,
      acceptedHighWater: previous.acceptedHighWater,
      sealId: previous.sealId
    });
  }

  find(actorId: string): TActor | undefined {
    return this.routes.get(actorId)?.actor;
  }

  route(actorId: string): ZLinkActorSessionRoute<TContext, TActor> | undefined {
    return this.routes.get(actorId);
  }

  unbind(actorId: string, context: TContext, bindingToken: string): void {
    const route = this.routes.get(actorId);
    if (route === undefined || route.context !== context || route.bindingToken !== bindingToken) {
      return;
    }
    this.routes.delete(actorId);
    context.unbindLocal(actorId, bindingToken);
  }

  unbindActor(actorId: string): void {
    const route = this.routes.get(actorId);
    if (route === undefined) {
      return;
    }
    this.unbind(actorId, route.context, route.bindingToken);
  }

  cleanup(context: TContext): void {
    for (const route of [...this.routes.values()]) {
      if (route.context === context) {
        this.unbind(route.actor.actorId, context, route.bindingToken);
      }
    }
  }

  requireRoute(actorId: string): ZLinkActorSessionRoute<TContext, TActor> {
    const route = this.routes.get(actorId);
    if (route !== undefined) {
      return route;
    }
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.ActorSessionNotBound,
      `No current session binding exists for actor '${actorId}'.`,
      true
    );
  }

  requireCurrentToken(actorId: string, bindingToken: string): void {
    const route = this.requireRoute(actorId);
    if (route.bindingToken === bindingToken) {
      return;
    }
    throw new ZLinkFrameworkException(
      ZLinkFrameworkErrorKind.ActorSessionNotBound,
      `Actor '${actorId}' session binding is stale.`,
      true
    );
  }

  accept(actorId: string, bindingToken: string): bigint {
    const route = this.requireRoute(actorId);
    if (route.bindingToken !== bindingToken) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorSessionNotBound,
        `Actor '${actorId}' session binding is stale.`,
        true
      );
    }
    if (route.sealId !== undefined) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorLocationStale,
        `Actor '${actorId}' session ingress is sealed for relocation.`,
        true
      );
    }
    route.acceptedHighWater++;
    return route.acceptedHighWater;
  }

  seal(actorId: string, sealId: string, expected: ZLinkActorSessionRouteFence): bigint {
    const route = this.requireRoute(actorId);
    if (route.sealId !== undefined) {
      if (route.sealId === sealId && routeMatchesFence(route, expected)) {
        return route.acceptedHighWater;
      }
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorLocationStale,
        `Actor '${actorId}' session ingress is sealed by another relocation.`,
        true
      );
    }
    if (!routeMatchesFence(route, expected)) {
      throw new ZLinkFrameworkException(
        ZLinkFrameworkErrorKind.ActorSessionNotBound,
        `Actor '${actorId}' session route seal was fenced by its binding identity.`,
        true
      );
    }
    route.sealId = sealId;
    return route.acceptedHighWater;
  }

  abortSeal(actorId: string, sealId: string): boolean {
    const route = this.routes.get(actorId);
    if (route === undefined || route.sealId !== sealId) return false;
    route.sealId = undefined;
    return true;
  }

  validateSeal(actorId: string, sealId: string, acceptedHighWater: bigint): boolean {
    const route = this.routes.get(actorId);
    return route !== undefined
      && route.sealId === sealId
      && route.acceptedHighWater === acceptedHighWater;
  }
}

export interface ZLinkActorSessionRouteFence {
  readonly objectGeneration: bigint;
  readonly authorityOwnerGeneration: bigint;
  readonly bindingGeneration: bigint;
  readonly ownerLeaseGeneration: bigint;
}

function routeMatchesFence<
  TContext extends ZLinkActorSessionBindingContext<TActor>,
  TActor extends ZLinkActorSessionBindingActor
>(route: ZLinkActorSessionRoute<TContext, TActor>, expected: ZLinkActorSessionRouteFence): boolean {
  const ref = (route.actor as TActor & { readonly ref?: unknown }).ref as {
    readonly generation?: bigint;
    readonly ownershipGeneration?: bigint;
    readonly bindingGeneration?: bigint;
    readonly ownerLeaseGeneration?: bigint;
  } | undefined;
  return ref !== undefined
    && BigInt(ref.generation ?? -1n) === expected.objectGeneration
    && ref.ownershipGeneration === expected.authorityOwnerGeneration
    && ref.bindingGeneration === expected.bindingGeneration
    && ref.ownerLeaseGeneration === expected.ownerLeaseGeneration;
}

function actorAcceptedHighWater<TActor extends ZLinkActorSessionBindingActor>(actor: TActor): bigint {
  const value = (actor as TActor & { readonly ref?: { readonly acceptedHighWater?: bigint } })
    .ref?.acceptedHighWater;
  return value === undefined || value < 0n ? 0n : value;
}
