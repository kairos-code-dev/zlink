'use strict';

const { parsePatternArgs, runPubSub } = require('./perf_main');

async function main() {
  const parsed = parsePatternArgs('PUBSUB', process.argv.slice(2));
  if (!parsed) {
    process.exit(1);
    return;
  }
  const { transport, size } = parsed;
  process.exit(await runPubSub(transport, size));
}

main().catch(() => process.exit(2));
