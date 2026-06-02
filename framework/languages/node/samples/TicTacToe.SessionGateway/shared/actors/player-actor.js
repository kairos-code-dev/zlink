class SessionPlayerActor {
  constructor(actorId, context) {
    this.actorId = actorId;
    this.context = context;
  }

  notifyTurn(cell) {
    return this.context.boundSession.send({ cell }).packetName('TurnPlaced').submit();
  }

  notifyOpponentJoined(matchId, opponentActorId, mark, state) {
    return this.context.boundSession
      .send({ matchId, opponentActorId, mark, state })
      .packetName('OpponentJoinedNotify')
      .submit();
  }

  notifyTurnChanged(matchId, turnActorId, state) {
    return this.context.boundSession
      .send({ matchId, turnActorId, state })
      .packetName('TurnChangedNotify')
      .submit();
  }

  notifyGameEnded(matchId, winnerActorId, draw, state) {
    return this.context.boundSession
      .send({ matchId, winnerActorId, draw, state })
      .packetName('GameEndedNotify')
      .submit();
  }
}

class SessionPlayerActorFactory {
  async create(actorId, context) {
    return new SessionPlayerActor(actorId, context);
  }
}

module.exports = { SessionPlayerActorFactory };
