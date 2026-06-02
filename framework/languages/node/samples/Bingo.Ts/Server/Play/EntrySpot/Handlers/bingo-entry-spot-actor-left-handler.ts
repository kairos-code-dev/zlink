class BingoEntrySpotActorLeftHandler {
  [key: string]: any;
  handle(actor) {
    return { actorId: actor.actorId };
  }
}

export { BingoEntrySpotActorLeftHandler };
