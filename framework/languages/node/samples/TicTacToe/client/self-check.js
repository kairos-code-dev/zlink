const assert = require('node:assert/strict');
const path = require('node:path');
const { startRoleProcess } = require('../../shared/role-process');

async function main() {
  const api = await startRoleProcess(path.resolve(__dirname, '../api-server/main.js'));
  const play = await startRoleProcess(path.resolve(__dirname, '../play-server/main.js'));

  try {
    assert.equal((await api.request({ command: 'ping' })).role, 'api-server');
    assert.equal((await play.request({ command: 'ping' })).role, 'play-server');

    const result = await play.request({ command: 'play' });
    assert.equal(result.match, 'match-ready');
    assert.equal(result.winner, 'p1');
    assert.equal(result.firstPacket, 'CreateMatch');
    assert.equal(result.timerRegistered, true);
    assert.deepEqual(result.notifications, [
      { packetName: 'PlayerJoinedNotify', target: 'p1', joined: 'p2' },
      { packetName: 'GameStateNotify', target: 'p2', winner: null },
      { packetName: 'GameStateNotify', target: 'p1', winner: null },
      { packetName: 'GameStateNotify', target: 'p2', winner: null },
      { packetName: 'GameStateNotify', target: 'p1', winner: null },
      { packetName: 'GameStateNotify', target: 'p2', winner: 'p1' }
    ]);
  } finally {
    await play.close();
    await api.close();
  }

  console.log('PASS TicTacToe');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
