class MatchBingoActorHandler {
  [key: string]: any;
  async handle(entrySpot, actor, request) {
    return await entrySpot.match(actor, request);
  }
}

export { MatchBingoActorHandler };
