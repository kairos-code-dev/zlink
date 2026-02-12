'use strict';

const { parsePatternArgs, runGateway } = require('./pair_bench');

async function main() {
  const parsed = parsePatternArgs('GATEWAY', process.argv.slice(2));
  if (!parsed) {
    process.exit(1);
    return;
  }
  const { transport, size } = parsed;
  process.exit(await runGateway(transport, size));
}

main().catch(() => process.exit(2));
