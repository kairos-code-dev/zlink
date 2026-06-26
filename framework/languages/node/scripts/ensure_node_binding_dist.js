const childProcess = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const nodeRoot = path.resolve(__dirname, '..');
const repoRoot = path.resolve(nodeRoot, '..', '..', '..');
const bindingRoot = path.join(repoRoot, 'bindings', 'node');
const npmCommand = process.platform === 'win32' ? 'npm.cmd' : 'npm';

function ensureNodeBindingDist() {
  if (hasRequiredDist()) {
    return;
  }
  const major = Number(process.versions.node.split('.')[0]);
  if (major < 22) {
    throw new Error(
      'bindings/node/dist is missing. Run the Node framework gate with Node 22 first so it can build the binding package.'
    );
  }
  console.log('-- preparing bindings/node dist');
  const result = childProcess.spawnSync(npmCommand, ['--prefix', bindingRoot, 'run', 'build'], {
    cwd: repoRoot,
    stdio: 'inherit',
    env: process.env
  });
  if (result.error) {
    console.error(`Failed to run ${npmCommand} for bindings/node build: ${result.error.message}`);
  }
  if (result.status !== 0) {
    process.exit(result.status ?? 1);
  }
  if (!hasRequiredDist()) {
    throw new Error('bindings/node build completed but required dist outputs are still missing.');
  }
}

function hasRequiredDist() {
  return [
    path.join(bindingRoot, 'dist', 'index.js'),
    path.join(bindingRoot, 'dist', 'index.d.ts')
  ].every((file) => fs.existsSync(file));
}

module.exports = { ensureNodeBindingDist };
