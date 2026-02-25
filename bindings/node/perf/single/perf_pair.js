'use strict';

const { parsePatternArgs, runPairLike, zlink } = require('./perf_main');

async function main() {
  const parsed = parsePatternArgs('PAIR', process.argv.slice(2));
  if (!parsed) {
    process.exit(1);
    return;
  }
  const { transport, size } = parsed;
  process.exit(await runPairLike('PAIR', zlink.SocketType.PAIR,
    zlink.SocketType.PAIR, transport, size));
}

main().catch(() => process.exit(2));
