class PlaceMarkHandler {
  constructor(matches) {
    this.matches = matches;
  }

  async handle(request) {
    const room = this.matches.require(request.matchId);
    const state = await room.place(request.actorId, request.cell);
    return {
      matchId: request.matchId,
      actorId: request.actorId,
      cell: request.cell,
      state
    };
  }
}

module.exports = { PlaceMarkHandler };
