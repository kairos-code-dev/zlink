const assert = require('node:assert/strict');
const path = require('node:path');
const { startRoleProcess } = require('../../shared/role-process');

async function main() {
  const registry = await startRoleProcess(path.resolve(__dirname, '../registry-server/main.js'));
  const session = await startRoleProcess(path.resolve(__dirname, '../session-server/main.js'));
  const play = await startRoleProcess(path.resolve(__dirname, '../play-server/main.js'));
  const api = await startRoleProcess(path.resolve(__dirname, '../api-server/main.js'));

  try {
    assert.equal((await registry.request({ command: 'ping' })).role, 'registry-server');
    assert.equal((await session.request({ command: 'ping' })).role, 'session-server');
    assert.equal((await play.request({ command: 'ping' })).role, 'play-server');

    const result = await api.request({ command: 'run' });
    assert.equal(result.room.status, 'Finished');
    assert.equal(result.room.hostActorId, 'p1');
    assert.deepEqual(result.room.winners, ['p1', 'p3']);
    assert.equal(result.earlyHostStartRejected, true);
    assert.equal(result.nonHostStartRejected, true);
    assert.deepEqual(result.room.players.map((player) => player.actorId), ['p1', 'p2', 'p3', 'p4']);

    const started = result.notifications.filter((message) => message.packetName === 'BingoGameStarted');
    const drawn = result.notifications.filter((message) => message.packetName === 'BingoNumberDrawn');
    const ended = result.notifications.filter((message) => message.packetName === 'BingoGameEnded');
    assert.deepEqual(started.map((message) => message.actorId), ['p1', 'p2', 'p3', 'p4']);
    assert.deepEqual(drawn.map((message) => message.actorId), ['p1', 'p2', 'p3', 'p4']);
    assert.deepEqual(ended.map((message) => message.actorId), ['p1', 'p2', 'p3', 'p4']);
  } finally {
    await api.close();
    await play.close();
    await session.close();
    await registry.close();
  }

  console.log('PASS Bingo');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
