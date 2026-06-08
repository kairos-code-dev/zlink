type BingoRouteHeader = {
  name: string;
};

type BingoSessionActor = {
  relay(header: BingoRouteHeader, payload: unknown): Promise<unknown>;
};

type BingoSessionContext = {
  actors: {
    bound: BingoSessionActor[];
  };
};

type BingoSessionHandlers = {
  tryHandle(context: BingoSessionContext, header: BingoRouteHeader, payload: unknown): Promise<boolean>;
};

class BingoSession {
  [key: string]: any;
  constructor(context: BingoSessionContext, handlers: BingoSessionHandlers) {
    this.context = context;
    this.handlers = handlers;
  }

  async dispatch(header: BingoRouteHeader, payload: unknown): Promise<unknown> {
    if (await this.handlers.tryHandle(this.context, header, payload)) {
      return;
    }

    const actor = this.requireSingleBoundActor(`relaying packet '${header.name}'`);
    return await actor.relay(header, payload);
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
