class TicTacToeEntrySpotActorJoinedHandler {
  handle(actorId) {
    return { actorId, joined: true };
  }
}

module.exports = { TicTacToeEntrySpotActorJoinedHandler };
