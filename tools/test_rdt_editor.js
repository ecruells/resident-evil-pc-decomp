// test_rdt_editor.js - headless tests for tools/rdt_event_editor.html.
//
//   node tools/test_rdt_editor.js            # assertions + full sweep
//   node tools/test_rdt_editor.js --quick    # assertions only
//
// Run from the repo root; it reads the RDTs under assets/USA/Stage*/.
//
// The editor is a single self-contained HTML page, so there is nothing to
// import. This extracts its one <script> block, stubs just enough DOM for the
// module to evaluate, and drives parse/rebuild directly.
//
// The property that matters most is that an UNEDITED round-trip is
// byte-identical. If that regresses, every save silently corrupts the file, and
// nothing in the UI would show it.
'use strict';
const fs = require('fs');
const path = require('path');

const HTML = path.join(__dirname, 'rdt_event_editor.html');
const js = /<script[^>]*>([\s\S]*?)<\/script>/.exec(fs.readFileSync(HTML, 'utf8'))[1];

const noop = () => {};
const el = new Proxy({}, {
  get: (t, k) => (k === 'appendChild' || k === 'addEventListener' || k === 'remove' ||
                  k === 'scrollIntoView') ? noop
               : (k === 'classList') ? { add: noop, remove: noop }
               : (k === 'value' || k === 'innerHTML' || k === 'textContent') ? ''
               : el,
  set: () => true,
});
global.document = { getElementById: () => el, createElement: () => el,
                    querySelectorAll: () => [], addEventListener: noop, body: el };
global.window = { addEventListener: noop };
global.alert = noop;
global.confirm = () => true;

const M = new Function(js + `
  return { parseRdt, rebuildRegion, buildFile, segBytes, scdFieldsFor,
           readField, writeField, setD: v => { D = v; } };
`)();

let fails = 0;
const ok = (c, m) => { console.log((c ? '  PASS  ' : '  FAIL  ') + m); if (!c) fails++; };
const same = (a, b) => {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
};
const roomFiles = () => {
  const root = path.join('assets', 'USA');
  let out = [];
  for (const d of fs.readdirSync(root).filter(x => /^Stage\d$/.test(x)))
    out = out.concat(fs.readdirSync(path.join(root, d))
      .filter(f => /^ROOM.*\.RDT$/i.test(f)).map(f => path.join(root, d, f)));
  return out;
};

// ---------------------------------------------------------------------------
// Room 1110's init script is the reference case: the mirror pair that needs
// both the SCD section support and the MSB-counted bit index to read correctly.
// ---------------------------------------------------------------------------
console.log('\n== room 1110 init: mirror decode and operand editing ==');
{
  const f = path.join('assets', 'USA', 'Stage1', 'ROOM1110.RDT');
  const bytes = new Uint8Array(fs.readFileSync(f));
  const D = M.parseRdt('ROOM1110.RDT', bytes, 0x60);
  M.setD(D);

  let mirror = null, biton = null;
  for (const seg of D.segs) for (const it of seg.items) {
    if (it.op === 0x0F) mirror = it;
    if (it.op === 0x05 && it.bytes[1] === 5) biton = it;
  }
  ok(!!mirror && !!biton, 'entities_0x0f and the bank-5 bit_op both found');
  ok(mirror && /axis=X/.test(mirror.note) && /plane=X=12500/.test(mirror.note),
     'mirror note: ' + (mirror ? mirror.note : '-'));
  ok(biton && /bit=0\(msbidx 31\)/.test(biton.note) && / set$/.test(biton.note),
     'bit_op note: ' + (biton ? biton.note : '-'));

  const F = M.scdFieldsFor(0x0F), args = mirror.bytes.slice(1);
  ok(M.readField(args, F[1]) === 4000,  'extent min = 4000');
  ok(M.readField(args, F[2]) === 6300,  'extent max = 6300');
  ok(M.readField(args, F[3]) === 12500, 'plane = 12500');

  const B = M.scdFieldsFor(0x05), bargs = biton.bytes.slice(1);
  ok(M.readField(bargs, B[1]) === 31, 'bit index (from MSB) = 31, i.e. bit 0');
  ok(M.readField(bargs, B[2]) === 0,  'byte offset = 0');
  ok(M.readField(bargs, B[3]) === 0,  'operation = 0 (set)');

  const before = bargs[1];
  M.writeField(bargs, B[1], 5);
  ok(bargs[1] === ((before & 0xE0) | 5), 'sub-byte write preserves the offset bits');
  M.writeField(bargs, B[1], 31);
  ok(bargs[1] === before, 'sub-byte write round-trips');
}

console.log('\n== an operand edit rewrites exactly the bytes it should ==');
{
  const f = path.join('assets', 'USA', 'Stage1', 'ROOM1110.RDT');
  const bytes = new Uint8Array(fs.readFileSync(f));
  const D = M.parseRdt('ROOM1110.RDT', bytes, 0x60);
  M.setD(D);
  let mirror = null;
  for (const seg of D.segs) for (const it of seg.items) if (it.op === 0x0F) mirror = it;
  mirror.bytes[1] = 0x03;                       // 0x02 -> 0x03
  const r = M.buildFile();
  const diffs = [];
  for (let i = 0; i < bytes.length; i++) if (r.out[i] !== bytes[i]) diffs.push(i);
  ok(r.fits, 'rebuilt section still fits in place');
  ok(diffs.length === 1 && diffs[0] === 0x20ff7 && r.out[0x20ff7] === 0x03,
     'one byte changed, at 0x20ff7, to 0x03');
}

console.log('\n== block size words are recomputed on rebuild ==');
{
  const bytes = new Uint8Array(fs.readFileSync(path.join('assets','USA','Stage1','ROOM1110.RDT')));
  const D = M.parseRdt('ROOM1110.RDT', bytes, 0x60);
  M.setD(D);
  const seg = D.segs[0];
  const n = M.segBytes(seg).length;
  seg.items[0].bytes.push(0x00);
  const r = M.rebuildRegion();
  ok((r.body[0] | (r.body[1] << 8)) === n + 1, 'size word tracks the block length');
}

// ---------------------------------------------------------------------------
// The sweep. Every room, every section: parse then rebuild with no edits and
// require the output to equal the input byte for byte. Sections that fail to
// parse are counted, not failed - plenty of rooms ship an empty main or event
// section, and a section whose opcode widths are not all known yet keeps its
// undecoded tail as one opaque item, which still round-trips.
// ---------------------------------------------------------------------------
if (process.argv.indexOf('--quick') < 0) {
  console.log('\n== unedited round-trip is byte-identical, all rooms ==');
  const per = { 0x60:{ok:0,absent:0,undec:0}, 0x64:{ok:0,absent:0,undec:0}, 0x68:{ok:0,absent:0,undec:0} };
  const bad = [];
  const files = roomFiles();
  for (const f of files) {
    const bytes = new Uint8Array(fs.readFileSync(f));
    for (const sec of [0x60, 0x64, 0x68]) {
      let D;
      try { D = M.parseRdt(path.basename(f), bytes, sec); }
      catch (e) { per[sec].absent++; continue; }
      try {
        M.setD(D);
        if (!same(M.buildFile().out, bytes)) { bad.push(path.basename(f) + ' 0x' + sec.toString(16)); continue; }
        per[sec].ok++;
        if (D.segs.some(g => g.items.some(i => /unknown width|undecoded/.test((i.name||'') + (i.note||'')))))
          per[sec].undec++;
      } catch (e) {
        bad.push(path.basename(f) + ' 0x' + sec.toString(16) + ' THREW ' + e.message);
      }
    }
  }
  for (const k of [0x60, 0x64, 0x68])
    console.log('    0x' + k.toString(16) + '  identical=' + per[k].ok +
                '  absent=' + per[k].absent + '  (with an undecoded tail: ' + per[k].undec + ')');
  ok(bad.length === 0, files.length + ' rooms swept, ' + bad.length + ' mismatches');
  bad.slice(0, 20).forEach(b => console.log('        ' + b));
}

console.log('\n' + (fails ? fails + ' FAILURE(S)' : 'all checks passed'));
process.exit(fails ? 1 : 0);
