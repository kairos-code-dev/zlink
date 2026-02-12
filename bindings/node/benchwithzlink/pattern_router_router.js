'use strict';

const { runFromArgs } = require('./pair_bench');

async function main() {
  let args = ['ROUTER_ROUTER', ...process.argv.slice(2)];
  if (args.length >= 4 && String(args[1]).toUpperCase() === 'ROUTER_ROUTER') {
    args = ['ROUTER_ROUTER', ...args.slice(2)];
  }
  process.exit(await runFromArgs(args));
}

main().catch(() => process.exit(2));
