class CreateMatchRoomHandler {
  constructor(matches) {
    this.matches = matches;
  }

  handle(request) {
    const room = this.matches.create(request.matchName ?? 'match');
    return {
      matchId: room.matchId,
      matchName: room.matchName,
      state: room.state()
    };
  }
}

module.exports = { CreateMatchRoomHandler };
