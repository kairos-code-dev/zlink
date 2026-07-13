import { BingoSamplePlayers, PacketNames } from '../../Shared/Contracts/messages';
const SampleNames = {
  apiChannel: 'bingo.api',
  playChannel: 'bingo.play',
  roomRouteChannel: 'bingo.room.route',
  roomRewardChannel: 'bingo.room.reward.publisher',
  roomRewardTopic: 'bingo.room.reward',
  playerActorType: 'bingo.player',
  roomSpotType: 'bingo.room',
  roomSpotNode: 'bingo.room',
  sessionStream: 'bingo.session.stream',
  sessionSpotNode: 'bingo.session',
  actorIds: [
    BingoSamplePlayers.player1,
    BingoSamplePlayers.player2,
    BingoSamplePlayers.observer,
    BingoSamplePlayers.drainProbe
  ],
  playerJoinedPacket: PacketNames.playerJoinedNotify,
  gameStartedPacket: PacketNames.gameStartedNotify,
  numberDrawnPacket: PacketNames.numberDrawnNotify,
  gameEndedPacket: PacketNames.gameEndedNotify
};

const SampleTimings = {
  requestTimeout: 20000
};

export { SampleNames, SampleTimings };
