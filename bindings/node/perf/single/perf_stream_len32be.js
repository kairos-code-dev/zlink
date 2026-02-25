'use strict';

const { parsePatternArgs, runStreamLen32Be } = require('./perf_main');

async function main() {
  const parsed = parsePatternArgs('STREAM_LEN32BE', process.argv.slice(2));
  if (!parsed) {
    process.exit(1);
    return;
  }
  const { transport, size } = parsed;
  process.exit(await runStreamLen32Be(transport, size));
}

main().catch(() => process.exit(2));
