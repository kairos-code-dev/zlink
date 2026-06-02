class BingoEntrySpotActorJoinedHandler {
  handle(actor) {
    return { actorId: actor.actorId };
  }
}

module.exports = { BingoEntrySpotActorJoinedHandler };
