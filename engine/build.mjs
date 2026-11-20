// Builds the SeedFindr engine with Zig's Clang (zig cc).
// Usage: node engine/build.mjs [program ...]   (default: every file in engine/src that has a main)
// Objects are cached in engine/build/obj and only rebuilt when the source is newer.
import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import os from 'node:os'

const ENGINE = path.dirname(new URL(import.meta.url).pathname.replace(/^\/(\w:)/, '$1'))
const LIB = path.join(ENGINE, 'cubiomes')
const SRC = path.join(ENGINE, 'src')
const OUT = path.join(ENGINE, 'build')
const OBJ = path.join(OUT, 'obj')

function findZig() {
  const fromPath = spawnSync('zig', ['version'], { shell: false })
  if (fromPath.status === 0) return 'zig'
  const pk = path.join(os.homedir(), 'AppData', 'Local', 'Microsoft', 'WinGet', 'Packages')
  if (fs.existsSync(pk)) {
    for (const d of fs.readdirSync(pk).filter((n) => n.startsWith('zig.zig_'))) {
      for (const sub of fs.readdirSync(path.join(pk, d))) {
        const exe = path.join(pk, d, sub, 'zig.exe')
        if (fs.existsSync(exe)) return exe
      }
    }
  }
  throw new Error('zig not found: winget install zig.zig')
}

const walk = (dir) =>
  fs.readdirSync(dir, { withFileTypes: true }).flatMap((e) => {
    const p = path.join(dir, e.name)
    return e.isDirectory() ? walk(p) : p.endsWith('.c') ? [p] : []
  })

// Library sources: everything except the fork's own test programs.
const libSources = walk(LIB).filter((f) => {
  const rel = path.relative(LIB, f).replaceAll('\\', '/')
  return rel !== 'tests.c' && !rel.startsWith('tests/')
})

const FLAGS = ['-O3', '-fwrapv', '-DNDEBUG', '-D_WIN32', '-mpopcnt', '-w'] // note: zig cc targets this CPU (native) by default; Phase 4 picks portable targets
const zig = findZig()
fs.mkdirSync(OBJ, { recursive: true })

function compile(src) {
  const rel = path.relative(ENGINE, src).replace(/[\\/]/g, '_').replace(/\.c$/, '.o')
  const obj = path.join(OBJ, rel)
  if (fs.existsSync(obj) && fs.statSync(obj).mtimeMs >= fs.statSync(src).mtimeMs) return obj
  const r = spawnSync(zig, ['cc', ...FLAGS, '-I', LIB, '-c', src, '-o', obj], { stdio: 'inherit' })
  if (r.status !== 0) throw new Error('compile failed: ' + src)
  return obj
}

// Compile in parallel batches (zig cc is single-threaded per file).
async function compileAll(files) {
  const { spawn } = await import('node:child_process')
  const jobs = Math.max(2, os.cpus().length - 2)
  const out = []
  let i = 0
  async function worker() {
    while (i < files.length) {
      const src = files[i++]
      const rel = path.relative(ENGINE, src).replace(/[\\/]/g, '_').replace(/\.c$/, '.o')
      const obj = path.join(OBJ, rel)
      out.push(obj)
      if (fs.existsSync(obj) && fs.statSync(obj).mtimeMs >= fs.statSync(src).mtimeMs) continue
      await new Promise((res, rej) => {
        const p = spawn(zig, ['cc', ...FLAGS, '-I', LIB, '-c', src, '-o', obj], { stdio: 'inherit' })
        p.on('exit', (c) => (c === 0 ? res() : rej(new Error('compile failed: ' + src))))
      })
    }
  }
  await Promise.all(Array.from({ length: jobs }, worker))
  return out
}

const programs = process.argv.slice(2).length
  ? process.argv.slice(2)
  : fs.readdirSync(SRC).filter((f) => f.endsWith('.c') && /int\s+main\s*\(/.test(fs.readFileSync(path.join(SRC, f), 'utf8'))).map((f) => f.replace(/\.c$/, ''))

const t0 = Date.now()
const libObjs = await compileAll(libSources)
const shared = fs.readdirSync(SRC).filter((f) => f.endsWith('.c') && !programs.includes(f.replace(/\.c$/, '')) && !/int\s+main\s*\(/.test(fs.readFileSync(path.join(SRC, f), 'utf8')))
const sharedObjs = shared.map((f) => compile(path.join(SRC, f)))
for (const prog of programs) {
  const obj = compile(path.join(SRC, prog + '.c'))
  const exe = path.join(OUT, prog + '.exe')
  const r = spawnSync(zig, ['cc', '-O3', '-o', exe, obj, ...sharedObjs, ...libObjs], { stdio: 'inherit' })
  if (r.status !== 0) throw new Error('link failed: ' + prog)
  console.log('built', path.relative(process.cwd(), exe))
}
console.log(`done in ${((Date.now() - t0) / 1000).toFixed(1)} s`)
