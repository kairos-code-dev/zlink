'use strict';

const { runFromArgs } = require('./pair_bench');

async function main() {
  let args = ['DEALER_DEALER', ...process.argv.slice(2)];
  if (args.length >= 4 && String(args[1]).toUpperCase() === 'DEALER_DEALER') {
    args = ['DEALER_DEALER', ...args.slice(2)];
  }
  process.exit(await runFromArgs(args));
}

main().catch(() => process.exit(2));
