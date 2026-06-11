class PlayerActor {
  [key: string]: any;
  constructor(actorId: string, displayName: string, actorRef: unknown) {
    this.actorId = actorId;
    this.displayName = displayName;
    this.actorRef = actorRef;
  }
}

export { PlayerActor };
