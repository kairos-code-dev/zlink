class SessionPlayerActor {
  constructor(actorId, context) {
    this.actorId = actorId;
    this.context = context;
  }

  notifyTurn(cell) {
    return this.context.boundSession
      .send({ cell })
      .packetName('TurnPlaced')
      .submit();
  }
}

class SessionPlayerActorFactory {
  async create(actorId, context) {
    return new SessionPlayerActor(actorId, context);
  }
}

module.exports = { SessionPlayerActorFactory };
