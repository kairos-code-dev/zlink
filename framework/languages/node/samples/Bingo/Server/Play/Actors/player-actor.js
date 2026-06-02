class PlayerActor {
  constructor(actorId, displayName, boundSession) {
    this.actorId = actorId;
    this.displayName = displayName;
    this.boundSession = boundSession;
  }
}

module.exports = { PlayerActor };
