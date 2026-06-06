class PlayActor {
  [key: string]: any;
  constructor(actorId, displayName) {
    this.actorId = actorId;
    this.displayName = displayName;
    this.notifications = [];
    this.nextSeq = 0;
    this.session = null;
  }

  push(packetName, payload) {
    this.nextSeq += 1;
    this.notifications.push({ seq: this.nextSeq, packetName, payload });
  }
}

export { PlayActor };
