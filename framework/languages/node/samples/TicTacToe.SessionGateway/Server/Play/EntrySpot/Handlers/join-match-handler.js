class JoinMatchHandler {
  constructor(actorManager, matches) {
    this.actorManager = actorManager;
    this.matches = matches;
  }

  async handle(request) {
    const actor = await this.actorManager.getOrCreate(request.actorId, 'player');
    const room = this.matches.require(request.matchId);
    const state = await room.join(actor);
    return {
      matchId: room.matchId,
      actorId: actor.actorId,
      state
    };
  }
}

module.exports = { JoinMatchHandler };
