// Quick demo of the SeedFindr engine until the app exists: pick a search, watch it hunt, get the best seeds with maps.
// Run with "Try SeedFindr.cmd" (or: node tools/try.mjs).
import { spawn, execFileSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import readline from 'node:readline/promises'

const ROOT = path.resolve(path.dirname(new URL(import.meta.url).pathname.replace(/^\/(\w:)/, '$1')), '..')
const HUNT = path.join(ROOT, 'engine', 'build', 'hunt.exe')
const MAP = path.join(ROOT, 'engine', 'build', 'map.exe')

const SEARCHES = [
  { name: 'Speedrun 1.16.1: village at spawn, bastion and fortress close in the Nether', version: '1.16.1', seconds: 30,
    args: ['--only', 'village_dist,bastion_dist,fortress_dist', '--require', 'village_dist<=64', '--require', 'bastion_dist<=96', '--require', 'fortress_dist<=160'] },
  { name: 'Broken End (26.3): no End island, or a hole in it (rare game bugs)', version: '26.3', seconds: 45,
    args: ['--only', 'end_missing_island,end_holed_island', '--min-score', '3'] },
  { name: 'Surprise me (26.3): anything unusual', version: '26.3', seconds: 30, args: ['--min-score', '3.5'] },
  { name: 'Surprise me (1.16.1): anything unusual', version: '1.16.1', seconds: 30, args: ['--min-score', '3.5'] },
  { name: 'Stacked spawn (26.3): lots of structures right around spawn', version: '26.3', seconds: 30,
    args: ['--only', 'structures_near_spawn', '--require', 'structures_near_spawn>=6'] },
]

if (!fs.existsSync(HUNT)) {
  console.log('The engine is not built yet. Ask the AI to run: node engine/build.mjs')
  process.exit(1)
}

const rl = readline.createInterface({ input: process.stdin, output: process.stdout })
console.log('\n  SeedFindr - engine demo\n')
SEARCHES.forEach((s, i) => console.log(`  ${i + 1}. ${s.name}`))
const pick = SEARCHES[(parseInt(await rl.question('\n  Pick a search (1-5) and press Enter: '), 10) || 1) - 1] ?? SEARCHES[0]
rl.close()

console.log(`\n  Hunting for ${pick.seconds} seconds on all CPU threads (the PC stays usable)...\n`)
const hits = []
let last = null
await new Promise((resolve) => {
  const p = spawn(HUNT, ['--version', pick.version, '--seconds', String(pick.seconds), '--progress-ms', '1000', ...pick.args])
  let buf = ''
  p.stdout.on('data', (d) => {
    buf += d
    let i
    while ((i = buf.indexOf('\n')) >= 0) {
      const line = buf.slice(0, i)
      buf = buf.slice(i + 1)
      let o
      try { o = JSON.parse(line) } catch { continue }
      if (o.type === 'hit') hits.push(o)
      if (o.type === 'progress' || o.type === 'done') {
        last = o
        process.stdout.write(`\r  ${o.seeds.toLocaleString()} seeds checked  |  ${Math.round(o.seedsPerSec ?? o.avgPerSec).toLocaleString()} per second  |  ${o.hits} found   `)
      }
    }
  })
  p.on('exit', resolve)
})
console.log('\n')

if (!hits.length) {
  console.log('  Nothing found this time. These are rare: try again or pick another search.')
} else {
  hits.sort((a, b) => b.score - a.score)
  const outDir = path.join(process.env.LOCALAPPDATA || '.', 'SeedFindr', 'try', new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19))
  fs.mkdirSync(outDir, { recursive: true })
  const lines = []
  const top = hits.slice(0, 5)
  top.forEach((h, i) => {
    const odds = h.score >= 1 ? `about 1 in ${Math.round(10 ** h.score).toLocaleString()} seeds` : 'common'
    lines.push(`${i + 1}. Seed ${h.seed}  (Minecraft ${h.version}, ${odds})`)
    for (const d of h.details) if (d.score > 0 || pick.args.includes('--require')) lines.push(`     - ${d.desc}${d.confirmInGame ? ' (needs a real-game check)' : ''}`)
    lines.push(`     Spawn: ${h.spawn[0]}, ${h.spawn[1]}`)
    if (i < 3) {
      const png = path.join(outDir, `${i + 1} - seed ${h.seed}.png`)
      try { execFileSync(MAP, [h.version, String(h.seed), png]); lines.push(`     Map: ${path.basename(png)}`) } catch {}
    }
    lines.push('')
  })
  const rate = last && last.seeds ? ` (about 1 in ${Math.round(last.seeds / hits.length).toLocaleString()} seeds match this search)` : ''
  const text = `SeedFindr demo: ${pick.name}\n${last ? last.seeds.toLocaleString() : '?'} seeds checked, ${hits.length} found${rate}. Best ${top.length}:\n\n${lines.join('\n')}`
  fs.writeFileSync(path.join(outDir, 'results.txt'), text)
  console.log('  ' + text.split('\n').join('\n  '))
  console.log('  Maps: white cross = spawn, coloured squares = structures (village orange, temples yellow/green, ruined portal purple...).')
  console.log('  Opening the results folder...')
  if (!process.env.SF_NO_OPEN) spawn('explorer.exe', [outDir], { detached: true, stdio: 'ignore' }).unref()
}
console.log('\n  Copy a seed into Minecraft (Create New World > More World Options > Seed) to see it.\n')
