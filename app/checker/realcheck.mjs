// Real-game check (stage 4, first version): runs the official Minecraft server for one seed and asks it,
// through the console, where the spawn and the nearest structures are. Ground truth for the engine.
//
//   node app/checker/realcheck.mjs <version> <seed> [--accept-eula]
//
// Downloads (once, cached in %LOCALAPPDATA%\SeedFindr\cache): the server jar from Mojang's version list and a
// matching portable Java (Eclipse Temurin JRE). The server only runs if the Minecraft EULA has been accepted
// (https://aka.ms/MinecraftEULA): pass --accept-eula once, which records the acceptance in the cache.
import { spawn, spawnSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import os from 'node:os'
import zlib from 'node:zlib'

const CACHE = path.join(process.env.LOCALAPPDATA || os.homedir(), 'SeedFindr', 'cache')
const EULA_FILE = path.join(CACHE, 'eula-accepted.txt')
fs.mkdirSync(CACHE, { recursive: true })

async function download(url, dest) {
  if (fs.existsSync(dest)) return dest
  const res = await fetch(url, { redirect: 'follow' })
  if (!res.ok) throw new Error(`download failed ${res.status}: ${url}`)
  const tmp = dest + '.part'
  fs.writeFileSync(tmp, Buffer.from(await res.arrayBuffer()))
  fs.renameSync(tmp, dest)
  return dest
}

async function serverJar(version) {
  const manifest = await (await fetch('https://piston-meta.mojang.com/mc/game/version_manifest_v2.json')).json()
  const entry = manifest.versions.find((v) => v.id === version)
  if (!entry) throw new Error('unknown Minecraft version ' + version)
  const meta = await (await fetch(entry.url)).json()
  const jar = await download(meta.downloads.server.url, path.join(CACHE, `server-${version}.jar`))
  return { jar, java: meta.javaVersion?.majorVersion ?? 8 }
}

async function javaExe(major) {
  const dir = path.join(CACHE, `java-${major}`)
  const find = () => {
    if (!fs.existsSync(dir)) return null
    for (const sub of fs.readdirSync(dir)) {
      const exe = path.join(dir, sub, 'bin', 'java.exe')
      if (fs.existsSync(exe)) return exe
    }
    return null
  }
  if (find()) return find()
  const zip = await download(
    `https://api.adoptium.net/v3/binary/latest/${major}/ga/windows/x64/jre/hotspot/normal/eclipse`,
    path.join(CACHE, `java-${major}.zip`),
  )
  fs.mkdirSync(dir, { recursive: true })
  const r = spawnSync(path.join(process.env.SystemRoot || 'C:\Windows', 'System32', 'tar.exe'), ['-xf', zip, '-C', dir], { stdio: 'inherit' })
  if (r.status !== 0) throw new Error('could not unpack Java')
  fs.rmSync(zip)
  return find()
}

// Minimal NBT scan: an Int tag (type 3) with the given name.
function nbtInt(buf, name) {
  const key = Buffer.concat([Buffer.from([3, 0, name.length]), Buffer.from(name)])
  const i = buf.indexOf(key)
  return i < 0 ? null : buf.readInt32BE(i + key.length)
}

function modern(version) {
  const [a, b] = version.split('.').map(Number)
  return a > 1 || b >= 19 // `locate structure` arrived in 1.19
}

// What to ask the server. [label, dimension, 1.16-style name, modern name]
const TARGETS = [
  ['village', 'overworld', 'village', '#minecraft:village'],
  ['desert_pyramid', 'overworld', 'desert_pyramid', 'minecraft:desert_pyramid'],
  ['jungle_pyramid', 'overworld', 'jungle_pyramid', 'minecraft:jungle_pyramid'],
  ['swamp_hut', 'overworld', 'swamp_hut', 'minecraft:swamp_hut'],
  ['igloo', 'overworld', 'igloo', 'minecraft:igloo'],
  ['shipwreck', 'overworld', 'shipwreck', '#minecraft:shipwreck'],
  ['ocean_ruin', 'overworld', 'ocean_ruin', '#minecraft:ocean_ruin'],
  ['buried_treasure', 'overworld', 'buried_treasure', 'minecraft:buried_treasure'],
  ['ruined_portal', 'overworld', 'ruined_portal', '#minecraft:ruined_portal'],
  ['pillager_outpost', 'overworld', 'pillager_outpost', 'minecraft:pillager_outpost'],
  ['monument', 'overworld', 'monument', 'minecraft:monument'],
  ['mansion', 'overworld', 'mansion', 'minecraft:mansion'],
  ['stronghold', 'overworld', 'stronghold', 'minecraft:stronghold'],
  ['fortress', 'the_nether', 'fortress', 'minecraft:fortress'],
  ['bastion_remnant', 'the_nether', 'bastion_remnant', 'minecraft:bastion_remnant'],
]

export const eulaAccepted = () => fs.existsSync(EULA_FILE)
export const acceptEula = () => fs.writeFileSync(EULA_FILE, `Minecraft EULA accepted by the user on ${new Date().toISOString()}\n`)

/**
 * Runs the official server for one seed and returns ground truth.
 * @param points  [{ label, x, z }]: predicted structures to confirm. For each, the server runs `locate` from that
 *                spot (returns the structure in that region if it exists) and, once per label, from 0,0.
 * @param labels  extra labels to locate from 0,0 (default: the labels in `points`).
 */
export async function realCheck(version, seed, { points = [], labels } = {}) {
  if (!eulaAccepted()) throw new Error('The Minecraft EULA must be accepted first.')
  const t0 = Date.now()
  const { jar, java } = await serverJar(version)
  const exe = await javaExe(java)
  const world = fs.mkdtempSync(path.join(os.tmpdir(), 'seedfindr-'))
  fs.writeFileSync(path.join(world, 'eula.txt'), 'eula=true\n')
  fs.writeFileSync(
    path.join(world, 'server.properties'),
    [`level-seed=${seed}`, 'online-mode=false', `server-port=${25600 + Math.floor(Math.random() * 300)}`, 'view-distance=3',
      'simulation-distance=3', 'spawn-protection=0', 'max-players=1', 'motd=SeedFindr check', 'sync-chunk-writes=false',
      'max-tick-time=-1'].join('\n') + '\n', // no watchdog: a far /locate can take over a minute in one tick
  )

  const proc = spawn(exe, ['-Xmx2G', '-jar', jar, 'nogui'], { cwd: world, stdio: ['pipe', 'pipe', 'pipe'] })
  let log = ''
  const waiters = []
  const onData = (d) => {
    log += d.toString()
    for (const w of [...waiters]) if (w.re.test(log.slice(w.from))) { waiters.splice(waiters.indexOf(w), 1); w.res(log.slice(w.from)) }
  }
  proc.stdout.on('data', onData)
  proc.stderr.on('data', onData)
  const waitFor = (re, from = log.length, ms = 300000) =>
    new Promise((res, rej) => {
      const timer = setTimeout(() => rej(new Error('timeout waiting for ' + re + '\n' + log.slice(-2000))), ms)
      const w = { re, from, res: (v) => { clearTimeout(timer); res(v) } }
      waiters.push(w)
      onData('')
    })
  const send = (cmd) => proc.stdin.write(cmd + '\n')

  await waitFor(/Done \(/, 0)
  const tReady = Date.now()
  const result = { version, seed, fromOrigin: {}, confirmed: [] }

  // Spawn: summon a marker entity at the console's position (the world spawn) and read its position.
  let from = log.length
  if (modern(version)) {
    // 1.17+: no spawn chunks stay loaded, so summon and read in one command.
    send('execute summon minecraft:marker run data get entity @s Pos')
  } else {
    send('summon minecraft:armor_stand ~ ~ ~ {Tags:["sf"],NoGravity:1b,Invisible:1b}')
    send('data get entity @e[tag=sf,limit=1] Pos')
  }
  const posOut = await waitFor(/Pos.*?\[.*?\]|has the following entity data: \[/, from, 30000)
  const m = posOut.match(/\[(-?[\d.]+)d, (-?[\d.]+)d, (-?[\d.]+)d\]/)
  if (m) result.spawn = { x: Math.floor(+m[1]), z: Math.floor(+m[3]) }

  const mod = modern(version)
  const locate = async (label, x, z) => {
    const t = TARGETS.find((t) => t[0] === label)
    if (!t) return undefined
    const from = log.length
    const cmd = mod ? `locate structure ${t[3]}` : `locate ${t[2]}`
    send(`execute in minecraft:${t[1]} positioned ${x} 64 ${z} run ${cmd}`)
    const out = await waitFor(/nearest .*? is at \[|Could not find|Unknown|Incorrect|no structure|There is no|error/i, from, 300000)
    const p = out.match(/is at \[(-?\d+), [^,]+, (-?\d+)\]/)
    return p ? [+p[1], +p[2]] : null
  }
  for (const label of labels ?? [...new Set(points.map((p) => p.label))]) result.fromOrigin[label] = await locate(label, 0, 0)
  for (const pt of points) {
    const at = await locate(pt.label, pt.x, pt.z)
    result.confirmed.push({ ...pt, game: at, match: !!at && Math.abs(at[0] - pt.x) <= 16 && Math.abs(at[1] - pt.z) <= 16 })
  }
  send('stop')
  await new Promise((r) => proc.on('exit', r))

  // Cross-check spawn with level.dat when available.
  const levelDat = path.join(world, 'world', 'level.dat')
  if (fs.existsSync(levelDat)) {
    const nbt = zlib.gunzipSync(fs.readFileSync(levelDat))
    const sx = nbtInt(nbt, 'SpawnX'), sz = nbtInt(nbt, 'SpawnZ')
    if (sx !== null) result.levelDatSpawn = { x: sx, z: sz }
  }
  result.seconds = { startup: (tReady - t0) / 1000, total: (Date.now() - t0) / 1000 }
  fs.rmSync(world, { recursive: true, force: true })
  return result
}

// CLI: node realcheck.mjs <version> <seed> [--accept-eula] [label:x:z ...]
if (import.meta.url === `file:///${process.argv[1].replace(/\\/g, '/')}`) {
  const args = process.argv.slice(2)
  const [version, seed] = args
  if (args.includes('--accept-eula')) acceptEula()
  if (!version || !seed) {
    console.error('usage: realcheck.mjs <version> <seed> [--accept-eula] [label:x:z ...]')
    process.exit(2)
  }
  if (!eulaAccepted()) {
    console.error('The Minecraft EULA (https://aka.ms/MinecraftEULA) must be accepted first: run once with --accept-eula.')
    process.exit(3)
  }
  const points = args.slice(2).filter((a) => /^\w+:-?\d+:-?\d+$/.test(a)).map((a) => {
    const [label, x, z] = a.split(':')
    return { label, x: +x, z: +z }
  })
  realCheck(version, seed, { points, labels: points.length ? undefined : TARGETS.map((t) => t[0]) })
    .then((r) => console.log(JSON.stringify(r, null, 2)))
    .catch((e) => {
      console.error(e.message)
      process.exit(1)
    })
}
