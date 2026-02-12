'use strict';

const { parsePatternArgs, runRouterRouter } = require('./pair_bench');

async function main() {
  const parsed = parsePatternArgs('ROUTER_ROUTER', process.argv.slice(2));
  if (!parsed) {
    process.exit(1);
    return;
  }
  const { transport, size } = parsed;
  process.exit(await runRouterRouter(transport, size, false));
}

main().catch(() => process.exit(2));
