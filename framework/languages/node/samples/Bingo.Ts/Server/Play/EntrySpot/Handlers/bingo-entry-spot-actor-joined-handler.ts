class BingoEntrySpotActorJoinedHandler {
  [key: string]: any;
  handle(actor) {
    return { actorId: actor.actorId };
  }
}

export { BingoEntrySpotActorJoinedHandler };
