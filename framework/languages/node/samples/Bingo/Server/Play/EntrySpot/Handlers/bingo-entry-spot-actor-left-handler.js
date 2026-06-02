class BingoEntrySpotActorLeftHandler {
  handle(actor) {
    return { actorId: actor.actorId };
  }
}

module.exports = { BingoEntrySpotActorLeftHandler };
