import 'reflect-metadata';
import { startWorkflowHost } from './workflow-host-factory';

startWorkflowHost(process.argv.slice(2)).catch((error: unknown) => {
  console.error(error);
  process.exitCode = 1;
});
