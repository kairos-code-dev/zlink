type BingoRouteHeader = {
  name: string;
};

type BingoSessionActor = {
  relay(header: BingoRouteHeader, payload: unknown): Promise<unknown>;
};

type BingoSessionContext = {
  actorId?: string | null;
  displayName?: string | null;
  actors: {
    bound: BingoSessionActor[];
  };
};

type BingoSessionHandlers = {
  tryHandle(context: BingoSessionContext, header: BingoRouteHeader, payload: unknown): Promise<boolean>;
};

class BingoSession {
  constructor(
    readonly context: BingoSessionContext,
    private readonly handlers: BingoSessionHandlers
  ) {}

  async dispatch(header: BingoRouteHeader, payload: unknown): Promise<unknown> {
    if (await this.handlers.tryHandle(this.context, header, payload)) {
      return;
    }

    const actor = this.requireSingleBoundActor(`relaying packet '${header.name}'`);
    return await actor.relay(header, payload);
  }

  onDisconnected(): void {
    this.context.actorId = null;
    this.context.displayName = null;
    this.context.actors.bound.length = 0;
  }

  requireSingleBoundActor(action: string): BingoSessionActor {
    const actors = this.context.actors.bound;
    if (actors.length === 1) {
      return actors[0];
    }
    if (actors.length === 0) {
      throw new Error(`Client must authenticate before ${action}.`);
    }
    throw new Error(`Exactly one actor must be bound before ${action}.`);
  }
}

export { BingoSession };
