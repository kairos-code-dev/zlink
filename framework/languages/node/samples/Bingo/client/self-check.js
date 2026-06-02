const assert = require('node:assert/strict');
const path = require('node:path');
const { withRoleProcess } = require('../../shared/role-process');

async function main() {
  await withRoleProcess(
    path.resolve(__dirname, '../api-server/main.js'),
    { command: 'run' },
    async (result) => {
      assert.equal(result.winner, 'p1');
      assert.deepEqual(result.notifications, [{
        actorId: 'p1',
        packetName: 'BingoWinner',
        payload: { winner: 'p1', number: 7 }
      }]);
    }
  );
    console.log('PASS Bingo');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
