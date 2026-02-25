'use strict';

const { parsePatternArgs, runStreamCallback } = require('./perf_main');

async function main() {
  const parsed = parsePatternArgs('STREAM_CALLBACK', process.argv.slice(2));
  if (!parsed) {
    process.exit(1);
    return;
  }
  const { transport, size } = parsed;
  process.exit(await runStreamCallback(transport, size));
}

main().catch(() => process.exit(2));
