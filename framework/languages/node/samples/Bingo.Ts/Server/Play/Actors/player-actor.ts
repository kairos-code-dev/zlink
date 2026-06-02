class PlayerActor {
  [key: string]: any;
  constructor(actorId, displayName, boundSession) {
    this.actorId = actorId;
    this.displayName = displayName;
    this.boundSession = boundSession;
  }
}

export { PlayerActor };
