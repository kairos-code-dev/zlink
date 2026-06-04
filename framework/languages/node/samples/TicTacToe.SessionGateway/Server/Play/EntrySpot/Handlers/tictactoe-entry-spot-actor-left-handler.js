class TicTacToeEntrySpotActorLeftHandler {
  handle(actorId) {
    return { actorId, left: true };
  }
}

module.exports = { TicTacToeEntrySpotActorLeftHandler };
