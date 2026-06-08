type ActorNotification = {
  seq: number;
  packetName: string;
  payload: unknown;
};

class PlayActor {
  [key: string]: any;
  constructor(actorId: string, displayName: string) {
    this.actorId = actorId;
    this.displayName = displayName;
    this.notifications = [];
    this.nextSeq = 0;
    this.session = null;
  }

  push(packetName: string, payload: unknown): void {
    this.nextSeq += 1;
    this.notifications.push({ seq: this.nextSeq, packetName, payload });
  }
}

export { PlayActor };
