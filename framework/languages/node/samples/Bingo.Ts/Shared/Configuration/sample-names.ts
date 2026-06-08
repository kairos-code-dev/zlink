const { PacketNames } = require('../Contracts/messages');

const SampleNames = {
  apiChannel: 'bingo.api',
  playChannel: 'bingo.play',
  registryChannel: 'bingo.registry',
  notificationChannel: 'bingo.notifications',
  apiService: 'bingo.api',
  playService: 'bingo.play',
  notificationService: 'bingo.notifications',
  playerActorType: 'bingo.player',
  roomSpotType: 'bingo.room',
  actorIds: ['player-1', 'player-2'],
  playerJoinedPacket: PacketNames.playerJoinedNotify,
  gameStartedPacket: PacketNames.gameStartedNotify,
  numberDrawnPacket: PacketNames.numberDrawnNotify,
  statePacket: PacketNames.stateNotify,
  gameEndedPacket: PacketNames.gameEndedNotify
};

const SampleTimings = {
  requestTimeout: 10000
};

export { SampleNames, SampleTimings };
