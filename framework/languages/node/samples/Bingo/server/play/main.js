const { startRouteServer } = require('../../../shared/route-runtime');
const { BingoCard } = require('../../shared/contracts/bingo-card');

async function main() {
  await startRouteServer({
    endpoint: process.env.BINGO_PLAY_ENDPOINT,
    routingId: 'play-server',
    handlers: [{ packetName: 'RunBingoRoom', handle: runBingoRoom }]
  });
}

async function runBingoRoom(request) {
  const players = request.players;
  const requiredPlayers = 4;
  const notifications = [];
  const room = {
    status: 'WaitingForPlayers',
    hostActorId: players[0].actorId,
    players: [],
    winners: []
  };
  room.players.push(players[0]);
  const earlyHostStartRejected = room.players.length !== requiredPlayers;
  room.players.push(...players.slice(1));
  const nonHostStartRejected = players[1].actorId !== room.hostActorId;
  room.status = 'Running';
  const cards = new Map(players.map((player) => [player.actorId, new BingoCard(player.numbers)]));
  broadcast(notifications, players, 'BingoGameStarted', { state: room });
  for (const number of request.draws) {
    for (const player of players) {
      if (cards.get(player.actorId).mark(number) && !room.winners.includes(player.actorId)) {
        room.winners.push(player.actorId);
      }
    }
    broadcast(notifications, players, 'BingoNumberDrawn', { number });
    if (room.winners.length > 0) {
      room.status = 'Finished';
      broadcast(notifications, players, 'BingoGameEnded', { state: room });
      break;
    }
  }
  return { room, earlyHostStartRejected, nonHostStartRejected, notifications };
}

function broadcast(notifications, players, packetName, payload) {
  for (const player of players) {
    notifications.push({ actorId: player.actorId, packetName, payload });
  }
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
