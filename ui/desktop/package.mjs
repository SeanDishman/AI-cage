import { packager } from '@electron/packager';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const ui = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const manifest = JSON.parse(await readFile(path.join(ui, 'package.json'), 'utf8'));
const generated = path.join(ui, 'build', 'desktop');
await mkdir(generated, { recursive: true });
await writeFile(path.join(generated, 'backend.json'), JSON.stringify({ controller: path.join(ui, '..', 'build', 'AICage.exe') }, null, 2));
const folders = await packager({
  dir: path.join(ui, 'desktop', 'app'), out: path.join(ui, 'dist'),
  name: 'AICage', executableName: 'AICage Desktop', platform: 'win32', arch: 'x64',
  electronVersion: manifest.devDependencies.electron, appVersion: '1.0.0',
  asar: true, overwrite: true, prune: false,
  extraResource: [path.join(ui, 'build', 'AICageUI.exe'), path.join(ui, 'web'), path.join(generated, 'backend.json')],
  win32metadata: { CompanyName: 'AICage', FileDescription: 'AICage desktop sandbox chat', ProductName: 'AICage' }
});
console.log(`Desktop application: ${path.join(folders[0], 'AICage Desktop.exe')}`);
