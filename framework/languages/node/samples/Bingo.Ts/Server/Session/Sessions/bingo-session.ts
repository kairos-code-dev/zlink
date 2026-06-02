class BingoSession {
  [key: string]: any;
  constructor(context, handlers) {
    this.context = context;
    this.handlers = handlers;
  }

  async dispatch(header, payload) {
    if (await this.handlers.tryHandle(this.context, header, payload)) {
      return;
    }

    const actor = this.requireSingleBoundActor(`relaying packet '${header.name}'`);
    return await actor.relay(header, payload);
  }

  requireSingleBoundActor(action) {
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
