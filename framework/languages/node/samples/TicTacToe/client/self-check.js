const assert = require('node:assert/strict');
const path = require('node:path');
const { withRoleProcess } = require('../../shared/role-process');

async function main() {
  await withRoleProcess(
    path.resolve(__dirname, '../server/main.js'),
    { command: 'play' },
    async (result) => {
      assert.equal(result.match, 'match-ready');
      assert.equal(result.winner, 'p1');
      assert.equal(result.firstPacket, 'CreateMatch');
    }
  );
    console.log('PASS TicTacToe');
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
