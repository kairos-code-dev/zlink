'use strict';

const { parsePatternArgs, runStream } = require('./pair_bench');

async function main() {
  const parsed = parsePatternArgs('STREAM', process.argv.slice(2));
  if (!parsed) {
    process.exit(1);
    return;
  }
  const { transport, size } = parsed;
  process.exit(await runStream(transport, size));
}

main().catch(() => process.exit(2));
