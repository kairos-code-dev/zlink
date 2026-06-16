import { BingoSamplePlayers, PacketNames } from '../../Shared/Contracts/messages';
const SampleNames = {
  apiChannel: 'bingo.api',
  playChannel: 'bingo.play',
  notificationChannel: 'bingo.notifications',
  playerActorType: 'bingo.player',
  roomSpotType: 'bingo.room',
  actorIds: [BingoSamplePlayers.player1, BingoSamplePlayers.player2],
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
