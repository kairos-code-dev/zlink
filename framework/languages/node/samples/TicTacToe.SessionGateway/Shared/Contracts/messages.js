const PacketNames = Object.freeze({
  authenticateActorReq: 'AuthenticateActorReq',
  createMatchReq: 'CreateMatchReq',
  ensurePlayerActorReq: 'EnsurePlayerActorReq',
  joinMatchReq: 'JoinMatchReq',
  placeMarkReq: 'PlaceMarkReq',
  authenticateSessionReq: 'AuthenticateSessionReq',
  createMatchSessionReq: 'CreateMatchSessionReq',
  placeMarkSessionReq: 'PlaceMarkSessionReq',
  notificationsReq: 'SessionGatewayNotificationsReq',
  opponentJoinedNotify: 'OpponentJoinedNotify',
  turnChangedNotify: 'TurnChangedNotify',
  gameEndedNotify: 'GameEndedNotify'
});

function actorDisplayName(actorId) {
  return actorId === 'p1' ? 'Player X' : 'Player O';
}

module.exports = { PacketNames, actorDisplayName };
