'use strict';

const { parsePatternArgs, runPairLike, zlink } = require('./pair_bench');

async function main() {
  const parsed = parsePatternArgs('DEALER_DEALER', process.argv.slice(2));
  if (!parsed) {
    process.exit(1);
    return;
  }
  const { transport, size } = parsed;
  process.exit(await runPairLike('DEALER_DEALER', zlink.SocketType.DEALER,
    zlink.SocketType.DEALER, transport, size));
}

main().catch(() => process.exit(2));
