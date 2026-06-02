const SampleNames = {
  apiChannel: 'bingo.api',
  playChannel: 'bingo.play',
  playerActorType: 'bingo.player',
  roomSpotType: 'bingo.room',
  actorIds: ['player-1', 'player-2', 'player-3', 'player-4'],
  playerJoinedPacket: 'PlayerJoinedNotify',
  gameStartedPacket: 'BingoGameStartedNotify',
  numberDrawnPacket: 'BingoNumberDrawnNotify',
  statePacket: 'BingoStateNotify',
  gameEndedPacket: 'BingoGameEndedNotify'
};

const SampleTimings = {
  requestTimeout: 10000
};

export { SampleNames, SampleTimings };
