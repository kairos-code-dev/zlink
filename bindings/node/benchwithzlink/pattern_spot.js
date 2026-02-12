'use strict';

const { parsePatternArgs, runSpot } = require('./pair_bench');

async function main() {
  const parsed = parsePatternArgs('SPOT', process.argv.slice(2));
  if (!parsed) {
    process.exit(1);
    return;
  }
  const { transport, size } = parsed;
  process.exit(await runSpot(transport, size));
}

main().catch(() => process.exit(2));
