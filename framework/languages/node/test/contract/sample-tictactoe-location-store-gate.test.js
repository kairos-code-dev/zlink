const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');

const nodeRoot = path.resolve(__dirname, '../..');

test('TicTacToe uses only the framework location store for room routing', () => {
  const sampleRoot = path.join(nodeRoot, 'samples/TicTacToe.Ts');
  const provisioner = fs.readFileSync(path.join(
    sampleRoot,
    'Server/Play/Infrastructure/ZLink/tictactoe-game-room-provisioner.ts'
  ), 'utf8');
  const obsoleteStore = path.join(sampleRoot, 'Server/Configuration/redis-room-route-store.ts');

  assert.equal(fs.existsSync(obsoleteStore), false);
  assert.match(provisioner, /spotManager\.getOrCreate\(TicTacToeGameSpot, roomId\)/);
  assert.doesNotMatch(provisioner, /RedisRoomRouteStore|room-route=verified|\.routes\./);
});
