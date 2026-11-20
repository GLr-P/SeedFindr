// Spot-check: compares what the engine predicts (probe.exe) with the official server for a few seeds.
//   node engine/tests/spotcheck.mjs <version> <seed> [<seed> ...]
// Needs the Minecraft EULA accepted (see app/checker/realcheck.mjs). Takes about a minute per seed.
import { execFileSync } from 'node:child_process'
import path from 'node:path'
import { realCheck } from '../../app/checker/realcheck.mjs'

const ROOT = path.resolve(path.dirname(new URL(import.meta.url).pathname.replace(/^\/(\w:)/, '$1')), '..', '..')
const PROBE = path.join(ROOT, 'engine', 'build', 'probe.exe')
const PER_TYPE = 3

const [version, ...seeds] = process.argv.slice(2)
if (!version || !seeds.length) {
  console.error('usage: spotcheck.mjs <version> <seed> [<seed> ...]')
  process.exit(2)
}

let fails = 0
for (const seed of seeds) {
  const probe = JSON.parse(execFileSync(PROBE, [version, seed], { encoding: 'utf8' }))
  const points = []
  for (const group of [probe.overworld, probe.nether])
    for (const [label, list] of Object.entries(group)) for (const [x, z] of list.slice(0, PER_TYPE)) points.push({ label, x, z })
  const [sx, sz] = probe.strongholds[0]
  points.push({ label: 'stronghold', x: sx, z: sz })

  const game = await realCheck(version, seed, { points })
  const lines = []
  const spawnOk = game.spawn && game.spawn.x === probe.spawn.x && game.spawn.z === probe.spawn.z
  if (!spawnOk) fails++
  lines.push(`spawn      engine ${probe.spawn.x},${probe.spawn.z}  game ${game.spawn?.x},${game.spawn?.z}  ${spawnOk ? 'OK' : 'MISMATCH'}`)
  for (const c of game.confirmed) {
    // Strongholds are not region-based: allow the engine's exact position to differ by a chunk.
    if (!c.match) fails++
    lines.push(`${c.label.padEnd(16)} engine ${c.x},${c.z}  game ${c.game ? c.game.join(',') : 'none'}  ${c.match ? 'OK' : 'MISMATCH'}`)
  }
  // The structure /locate finds from 0,0 must be one the engine predicted (if it lies inside the engine's radius).
  for (const [label, at] of Object.entries(game.fromOrigin)) {
    if (!at || label === 'stronghold') continue
    const list = (probe.overworld[label] ?? probe.nether[label] ?? [])
    const known = list.some(([x, z]) => Math.abs(x - at[0]) <= 16 && Math.abs(z - at[1]) <= 16)
    const far = Math.hypot(at[0], at[1]) > (probe.nether[label] ? 1000 : 2000) || list.length >= 6
    if (!known && !far) fails++
    lines.push(`from 0,0 ${label.padEnd(16)} game ${at.join(',')}  ${known ? 'OK (predicted)' : far ? 'outside engine radius' : 'MISSED BY ENGINE'}`)
  }
  console.log(`\n${version} seed ${seed} (server ${game.seconds.total.toFixed(0)} s)\n  ` + lines.join('\n  '))
}
console.log(`\n${fails ? fails + ' mismatch(es)' : 'all match'}`)
process.exit(fails ? 1 : 0)
