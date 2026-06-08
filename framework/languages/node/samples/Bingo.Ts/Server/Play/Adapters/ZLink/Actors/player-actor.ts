class PlayerActor {
  [key: string]: any;
  constructor(actorId: string, displayName: string, boundSession: any) {
    this.actorId = actorId;
    this.displayName = displayName;
    this.boundSession = boundSession;
  }
}

export { PlayerActor };
