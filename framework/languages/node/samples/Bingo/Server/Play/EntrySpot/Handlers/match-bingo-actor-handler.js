class MatchBingoActorHandler {
  async handle(entrySpot, actor, request) {
    return await entrySpot.match(actor, request);
  }
}

module.exports = { MatchBingoActorHandler };
