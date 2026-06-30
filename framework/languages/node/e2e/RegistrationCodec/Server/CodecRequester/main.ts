import { startCodecRequester } from './codec-requester-host-factory';

startCodecRequester(process.argv.slice(2)).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
