class PlayActorJoinGameHandler {
  handle({ notifications }, request) {
    notifications.push({
      packetName: 'PlayerJoinedNotify',
      target: request.actorId,
      joined: request.opponentActorId
    });
    return { matchId: request.matchId, actorId: request.actorId };
  }
}

module.exports = { PlayActorJoinGameHandler };
