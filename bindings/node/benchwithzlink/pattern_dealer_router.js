'use strict';

const { parsePatternArgs, runDealerRouter } = require('./pair_bench');

async function main() {
  const parsed = parsePatternArgs('DEALER_ROUTER', process.argv.slice(2));
  if (!parsed) {
    process.exit(1);
    return;
  }
  const { transport, size } = parsed;
  process.exit(await runDealerRouter(transport, size));
}

main().catch(() => process.exit(2));
