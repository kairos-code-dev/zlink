'use strict';

const { runWithArgs } = require('./perf_multi_main');

const PATTERN = 'MULTI_SPOT';

async function main() {
  if (process.argv.length < 4) {
    return 1;
  }
  const transport = process.argv[2];
  const size = process.argv[3];
  const rest = process.argv.slice(4);
  const argv = [
    '--role', 'client',
    '--pattern', PATTERN,
    '--transport', transport,
    '--size', size,
    ...rest,
  ];
  return runWithArgs(argv);
}

main()
  .then((rc) => process.exit(rc))
  .catch(() => process.exit(2));
