const { runRoleServer } = require('../../shared/role-process');
const { createBingoServer } = require('./bingo-server');

async function main() {
  await runRoleServer(
    () => createBingoServer(),
    { run: runDeterministicBingo }
  );
}

async function runDeterministicBingo(server) {
  const players = [
    { actorId: 'p1', numbers: [7] },
    { actorId: 'p2', numbers: [9] },
    { actorId: 'p3', numbers: [7] },
    { actorId: 'p4', numbers: [11] }
  ];

  for (const player of players) {
    await server.bindSession(player.actorId, `session-${player.actorId}`, 1);
  }

  const room = await server.spots.create(server.BingoRoomSpot);
  let earlyHostStartRejected = false;
  let nonHostStartRejected = false;
  let endedTask;
  await server.spots.executeOnSpot(room.spotRid, async (spot) => {
    spot.join(players[0].actorId, players[0].numbers, server.boundSessionFor(players[0].actorId));
    earlyHostStartRejected = await isRejected(() => spot.start(players[0].actorId, [7, 9, 11]));
    for (const player of players.slice(1)) {
      spot.join(player.actorId, player.numbers, server.boundSessionFor(player.actorId));
    }
    nonHostStartRejected = await isRejected(() => spot.start(players[1].actorId, [7, 9, 11]));
    endedTask = spot.start(players[0].actorId, [7, 9, 11]);
  });
  const ended = await endedTask;

  return {
    room: ended,
    earlyHostStartRejected,
    nonHostStartRejected,
    notifications: server.boundSessions.delivered.map((message) => ({
      actorId: message.actorId,
      packetName: message.packetName,
      payload: message.payload
    }))
  };
}

async function isRejected(action) {
  try {
    await action();
    return false;
  } catch {
    return true;
  }
}

main().catch((error) => {
  process.stdout.write(`${JSON.stringify({ event: 'error', message: error.message })}\n`);
  process.exitCode = 1;
});
