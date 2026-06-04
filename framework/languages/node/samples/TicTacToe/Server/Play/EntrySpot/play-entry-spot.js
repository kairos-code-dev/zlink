class PlayEntrySpot {
  constructor(joinHandler) {
    this.joinHandler = joinHandler;
  }

  join(actor, gameId) {
    return this.joinHandler.handle({ actor, gameId });
  }
}

module.exports = { PlayEntrySpot };
