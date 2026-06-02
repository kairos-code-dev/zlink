class BingoNotificationInbox {
  [key: string]: any;
  constructor(actorId) {
    this.actorId = actorId;
    this.playerJoined = [];
    this.started = [];
    this.drawn = [];
    this.ended = [];
  }

  apply(delivered) {
    for (const entry of delivered.filter((item) => item.actorId === this.actorId)) {
      if (entry.packetName === 'PlayerJoinedNotify') {
        this.playerJoined.push(entry.payload);
      } else if (entry.packetName === 'BingoGameStartedNotify') {
        this.started.push(entry.payload);
      } else if (entry.packetName === 'BingoNumberDrawnNotify') {
        this.drawn.push(entry.payload);
      } else if (entry.packetName === 'BingoGameEndedNotify') {
        this.ended.push(entry.payload);
      }
    }
  }
}

export { BingoNotificationInbox };
