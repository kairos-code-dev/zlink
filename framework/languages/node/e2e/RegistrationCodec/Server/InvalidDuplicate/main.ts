import { startInvalidDuplicate } from './invalid-duplicate-host-factory';

startInvalidDuplicate(process.argv.slice(2)).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
