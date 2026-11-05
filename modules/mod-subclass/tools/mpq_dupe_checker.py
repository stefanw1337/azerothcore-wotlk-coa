"""MPQ-DupeChecker: lists every file that more than one client patch carries, shows which patch wins and how the
copies differ, and merges duplicated DBC tables row by row into one patch that loads after all of them.
    python mpq_dupe_checker.py <client Data folder> [port]
It opens in the browser; nothing in Data changes until Merge is pressed."""
import difflib
import hashlib
import json
import struct
import sys
import threading
import webbrowser
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

sys.path.insert(0, str(Path(__file__).resolve().parent))

from mkmergepatch import LISTFILE, verified, written
from mpqpull import Archive, patch_order

MERGED_NAME = "patch-ZZZZZX.MPQ"
LOCALE_FOLDERS = ("enUS", "enGB")
BLIZZARD = {"common", "common-2", "expansion", "lichking", "patch", "patch-2", "patch-3"}
TEXT_SUFFIXES = (".lua", ".xml", ".toc", ".txt", ".wtf", ".ini")
SHOWN_ROWS = 300


def ordered_archives(data):
    root = sorted((path for path in data.iterdir() if path.suffix.lower() == ".mpq"), key=patch_order)
    locales = []
    for locale in LOCALE_FOLDERS:
        folder = data / locale
        if folder.is_dir():
            locales += sorted((path for path in folder.iterdir() if path.suffix.lower() == ".mpq"),
                              key=lambda path: path.stem.lower())
    base = [path for path in root if patch_order(path)[0] == 0]
    archives = base + locales + [path for path in root if patch_order(path)[0] != 0]
    return [path for path in archives if path.name.lower() != MERGED_NAME.lower()]


class Catalog:
    def __init__(self, data):
        self.data = Path(data)
        self.archives, self.names, self.problems = [], {}, []
        self.progress, self.done, self.lock = "", False, threading.Lock()
        self.opened, self.sizes = {}, {}
        self.users = 0

    def __enter__(self):
        with self.lock:
            self.users += 1
        return self

    def __exit__(self, *_):
        with self.lock:
            self.users -= 1
            if self.users == 0:
                for archive in self.opened.values():
                    archive.close()
                self.opened.clear()

    def archive(self, path):
        with self.lock:
            if path not in self.opened:
                self.opened[path] = Archive(str(path))
            return self.opened[path]

    def scan(self):
        with self:
            self.scan_archives()

    def scan_archives(self):
        names = {}
        archives = ordered_archives(self.data)
        for index, path in enumerate(archives):
            self.progress = f"{index + 1}/{len(archives)} {path.name}"
            try:
                listing = self.archive(path).extract(LISTFILE)
            except (OSError, struct.error, ValueError, zlib.error) as problem:
                self.problems.append(f"{path.name}: {problem}")
                continue
            if listing is None:
                self.problems.append(f"{path.name}: no listfile, its files cannot be listed")
                continue
            for line in listing.decode("latin1").replace(";", "\n").splitlines():
                name = line.strip()
                if name and name != LISTFILE:
                    names.setdefault(name.lower(), [name, []])[1].append(path)
        self.archives, self.names = archives, names
        self.progress, self.done = f"{len(archives)} patches, {len(names)} files", True

    def size_in(self, path, name):
        key = (path, name.lower())
        if key not in self.sizes:
            archive = self.archive(path)
            block = archive.locate(name)
            self.sizes[key] = archive.blocks[block][2] if block is not None else None
        return self.sizes[key]

    def duplicates(self, text, custom_only, differing_only):
        rows = []
        for name, paths in self.names.values():
            if len(paths) < 2 or (text and text not in name.lower()):
                continue
            if custom_only and all(path.stem.lower() in BLIZZARD for path in paths):
                continue
            sizes = [self.size_in(path, name) for path in paths]
            if differing_only and len(set(sizes)) == 1:
                continue
            rows.append({"name": name, "copies": [{"archive": self.label(path), "size": size}
                                                   for path, size in zip(paths, sizes)]})
        return sorted(rows, key=lambda row: row["name"].lower())

    def label(self, path):
        return str(path.relative_to(self.data))

    def copy(self, name, label):
        return self.archive(self.data / label).extract(name)

    def copies(self, name):
        return [(self.label(path), self.archive(path).extract(name)) for path in self.names[name.lower()][1]]


class Table:
    def __init__(self, data):
        if data[:4] != b"WDBC":
            raise ValueError("not a WDBC table")
        self.count, self.fields, self.size, string_size = struct.unpack_from("<4I", data, 4)
        self.strings = data[20 + self.count * self.size:20 + self.count * self.size + string_size]
        self.words = self.size == self.fields * 4
        self.rows = {}
        for index in range(self.count):
            record = data[20 + index * self.size:20 + (index + 1) * self.size]
            self.rows.setdefault(struct.unpack_from("<I", record)[0], record)
        self.string_slots = self.voted_string_slots()

    def voted_string_slots(self):
        if not self.words or not self.rows:
            return set()
        votes = {}
        for record in self.rows.values():
            for slot, value in enumerate(self.values(record)):
                if value and self.text(value) is not None:
                    votes[slot] = votes.get(slot, 0) + 1
        return {slot for slot, count in votes.items() if count * 2 >= len(self.rows)}

    def values(self, record):
        if not self.words:
            return [record.hex()]
        return list(struct.unpack(f"<{self.fields}I", record))

    def text(self, value):
        if 0 < value < len(self.strings) and self.strings[value - 1] == 0:
            return self.strings[value:self.strings.index(b"\0", value)].decode("latin1")
        return None

    def shown(self, value):
        text = self.text(value) if self.words else None
        return f"{value} '{text}'" if text else str(value)

    def same(self, other, record, theirs):
        if record == theirs:
            return True
        if not (self.words and other.words) or self.fields != other.fields:
            return False
        for mine, their in zip(self.values(record), other.values(theirs)):
            if mine != their and (self.text(mine) is None or self.text(mine) != other.text(their)):
                return False
        return True


def table_difference(older, newer):
    a, b = Table(older), Table(newer)
    if a.fields != b.fields or a.size != b.size:
        return {"kind": "table", "layout": f"{a.fields} fields/{a.size} bytes vs {b.fields} fields/{b.size} bytes",
                "only_a": len(set(a.rows) - set(b.rows)), "only_b": len(set(b.rows) - set(a.rows)),
                "changed": None, "fields": [], "rows": []}
    only_a, only_b = sorted(set(a.rows) - set(b.rows)), sorted(set(b.rows) - set(a.rows))
    changed, fields, shown = 0, {}, []
    for key in sorted(set(a.rows) & set(b.rows)):
        if a.same(b, a.rows[key], b.rows[key]):
            continue
        changed += 1
        differing = [(slot, x, y) for slot, (x, y) in enumerate(zip(a.values(a.rows[key]), b.values(b.rows[key])))
                     if x != y and not (a.words and a.text(x) is not None and a.text(x) == b.text(y))]
        for slot, _, _ in differing:
            fields[slot] = fields.get(slot, 0) + 1
        if len(shown) < SHOWN_ROWS:
            shown.append({"id": key, "fields": [[slot, a.shown(x), b.shown(y)] for slot, x, y in differing[:12]]})
    return {"kind": "table", "layout": f"{a.fields} fields, {a.size} bytes per row",
            "only_a": len(only_a), "only_b": len(only_b), "only_a_ids": only_a[:60], "only_b_ids": only_b[:60],
            "changed": changed, "fields": sorted(fields.items(), key=lambda item: -item[1])[:20], "rows": shown}


def difference(name, older, newer):
    if older is None or newer is None:
        return {"kind": "missing"}
    summary = {"same": older == newer, "md5_a": hashlib.md5(older).hexdigest(), "md5_b": hashlib.md5(newer).hexdigest(),
               "size_a": len(older), "size_b": len(newer)}
    if older == newer:
        return {"kind": "same", **summary}
    lowered = name.lower()
    if lowered.endswith(".dbc"):
        try:
            return {**table_difference(older, newer), **summary}
        except (ValueError, struct.error) as problem:
            return {"kind": "binary", "problem": str(problem), **summary}
    if lowered.endswith(TEXT_SUFFIXES):
        lines = difflib.unified_diff(older.decode("latin1").splitlines(), newer.decode("latin1").splitlines(),
                                     "older", "newer", lineterm="", n=2)
        return {"kind": "text", "diff": "\n".join(list(lines)[:4000]), **summary}
    return {"kind": "binary", **summary}


def merged_table(copies):
    tables = [(label, Table(data)) for label, data in copies]
    winner_label, winner = tables[-1]
    rows, report = dict(winner.rows), {"winner": winner_label, "added": {}, "kept_from_winner": 0, "skipped": []}
    strings = {key: winner for key in winner.rows}
    for label, table in reversed(tables[:-1]):
        if table.fields != winner.fields or table.size != winner.size:
            report["skipped"].append(f"{label}: another row layout")
            continue
        added = 0
        for key, record in table.rows.items():
            if key in rows:
                if not table.same(strings[key], record, rows[key]):
                    report["kept_from_winner"] += 1
                continue
            rows[key], strings[key] = record, table
            added += 1
        report["added"][label] = added
    block, offsets, body = bytearray(b"\0"), {b"": 0}, bytearray()
    for key in sorted(rows):
        source, record = strings[key], rows[key]
        if winner.words:
            values = list(struct.unpack(f"<{winner.fields}I", record))
            for slot in winner.string_slots:
                text = (source.text(values[slot]) or "").encode("latin1") if values[slot] else b""
                if text not in offsets:
                    offsets[text] = len(block)
                    block += text + b"\0"
                values[slot] = offsets[text]
            record = struct.pack(f"<{winner.fields}I", *values)
        body += record
    header = struct.pack("<4s4I", b"WDBC", len(rows), winner.fields, winner.size, len(block))
    return header + bytes(body) + bytes(block), report


def merge_targets(catalog):
    custom = [catalog.label(path) for path in catalog.archives if path.stem.lower() not in BLIZZARD]
    return [MERGED_NAME] + [label for label in custom if label.lower() != MERGED_NAME.lower()]


def merge(catalog, names, target):
    entries, reports = {}, []
    for name in names:
        copies = [(label, data) for label, data in catalog.copies(name) if data is not None]
        if len(copies) < 2 or not name.lower().endswith(".dbc"):
            reports.append({"name": name, "error": "only DBC tables with two or more copies can be merged"})
            continue
        data, report = merged_table(copies)
        entries[name.lower()] = (catalog.names[name.lower()][0], data)
        reports.append({"name": name, **report})
    if not entries:
        return reports
    output = (catalog.data / target).resolve()
    if output.parent not in (catalog.data.resolve(), *(catalog.data.resolve() / locale for locale in LOCALE_FOLDERS)):
        return reports + [{"name": target, "error": "the target has to be a patch in the Data folder"}]
    if output.exists():
        archive = catalog.archive(catalog.data / target)
        for lowered, name in names_in_archive(archive).items():
            if lowered not in entries:
                entries[lowered] = (name, archive.extract(name))
        with catalog.lock:
            opened = catalog.opened.pop(catalog.data / target, None)
            if opened:
                opened.close()
            catalog.sizes.clear()
        backup = output.with_name(output.name + ".bak")
        backup.write_bytes(output.read_bytes())
    files = sorted(entries.values(), key=lambda entry: entry[0].lower())
    files.append((LISTFILE, "\r\n".join(name for name, _ in files).encode("latin1")))
    staged = output.with_name(output.name + ".merging")
    staged.write_bytes(written(files))
    verified(staged, files)
    staged.replace(output)
    later = [catalog.label(path) for path in catalog.archives if patch_order(path) > patch_order(output)
             and any(path in catalog.names.get(lowered, ["", []])[1] for lowered in names_lower(names))]
    reports.append({"name": target, "written": str(output), "files": len(files) - 1, "later": later})
    return reports


def names_lower(names):
    return [name.lower() for name in names]


def names_in_archive(archive):
    listing = archive.extract(LISTFILE)
    found = {}
    for line in (listing or b"").decode("latin1").replace(";", "\n").splitlines():
        name = line.strip()
        if name and name != LISTFILE:
            found.setdefault(name.lower(), name)
    return found


PAGE = """<!doctype html><html><head><meta charset="utf-8"><title>MPQ-DupeChecker</title><style>
body{font:14px system-ui,sans-serif;margin:0;background:#16181d;color:#e6e6e6}header{padding:12px 16px;background:#20232b;
display:flex;gap:12px;align-items:center;flex-wrap:wrap}h1{font-size:18px;margin:0 12px 0 0}input[type=text]{padding:6px;
width:260px;background:#111;color:#eee;border:1px solid #444}button{padding:6px 12px;background:#3a5f9e;color:#fff;border:0;
border-radius:4px;cursor:pointer}button.warn{background:#9e5a3a}main{display:flex;height:calc(100vh - 58px)}#list{width:42%;
overflow:auto;border-right:1px solid #333}#detail{flex:1;overflow:auto;padding:12px 16px}.row{padding:6px 10px;
border-bottom:1px solid #262a33;cursor:pointer}.row:hover,.row.on{background:#26303f}.copies{color:#9aa;font-size:12px}
.win{color:#7fd17f}.diff{color:#e0a060}table{border-collapse:collapse;margin:8px 0}td,th{border:1px solid #333;
padding:3px 6px;font:12px monospace;vertical-align:top}pre{white-space:pre-wrap;font:12px monospace;background:#111;
padding:8px}.add{color:#7fd17f}.del{color:#e07070}#status{color:#9aa}label{color:#ccc}</style></head><body><header>
<h1>MPQ-DupeChecker</h1><input id="q" type="text" placeholder="Filter, e.g. spell.dbc"><label><input id="custom"
type="checkbox" checked> skip Blizzard-only</label><label><input id="differ" type="checkbox" checked> only differing
size</label><button onclick="load()">Refresh</button><label>Merge into <select id="target"></select></label><button class="warn" onclick="mergeChecked()">Merge
checked DBCs</button><span id="status"></span></header><main><div id="list"></div><div id="detail">Pick a file on the left. The last
patch listed wins in the game.</div></main><script>
const $=id=>document.getElementById(id);let rows=[];
async function poll(){const s=await (await fetch('/api/status')).json();$('status').textContent=s.progress+
(s.problems.length?' - '+s.problems.length+' patch(es) not listed':'');if(!s.done){setTimeout(poll,700);return}load()}
async function targets(){const t=await (await fetch('/api/targets')).json();$('target').innerHTML=t.map(x=>
`<option>${esc(x)}</option>`).join('')}
async function load(){if(!$('target').options.length)targets();const p=new URLSearchParams({q:$('q').value.toLowerCase(),custom:$('custom').checked?1:0,
differ:$('differ').checked?1:0});rows=await (await fetch('/api/dupes?'+p)).json();$('list').innerHTML=rows.map((r,i)=>
`<div class="row" onclick="show(${i},this)">${r.name.toLowerCase().endsWith('.dbc')?`<input type=checkbox data-n="${r.name}"
onclick="event.stopPropagation()"> `:''}${r.name}<div class="copies">${r.copies.map((c,j)=>`<span class="${j==r.copies.
length-1?'win':''}">${c.archive} (${c.size})</span>`).join(' &rarr; ')}</div></div>`).join('')||'No duplicates match.';
$('status').textContent=rows.length+' duplicated file(s)'}
function esc(s){return String(s).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]))}
async function show(i,el){document.querySelectorAll('.row.on').forEach(x=>x.classList.remove('on'));el.classList.add('on');
const r=rows[i];const n=r.copies.length;$('detail').innerHTML=`<h2>${esc(r.name)}</h2>Compare <select id=a>${r.copies.map
((c,j)=>`<option ${j==n-2?'selected':''}>${c.archive}</option>`).join('')}</select> with <select id=b>${r.copies.map((c,j)=>
`<option ${j==n-1?'selected':''}>${c.archive}</option>`).join('')}</select> <button onclick="compare('${esc(r.name).replace(/\\\\/g,
'\\\\\\\\')}')">Compare</button> <button onclick="saveBoth(rows[${i}].name)">Save both</button><div id=out></div>`;
compare(r.name)}
async function saveBoth(name){const r=await (await fetch('/api/save?'+new URLSearchParams({name,a:$('a').value,b:$('b').value}
))).json();$('status').textContent='Saved to '+r.join(' and ')}
async function compare(name){$('out').textContent='Reading...';const d=await (await fetch('/api/diff?'+new URLSearchParams(
{name,a:$('a').value,b:$('b').value}))).json();let h=`<p>${esc($('a').value)}: ${d.size_a} bytes, md5 ${d.md5_a}<br>${esc(
$('b').value)}: ${d.size_b} bytes, md5 ${d.md5_b}</p>`;if(d.kind=='same')h+='<p class=win>Identical.</p>';if(d.kind=='text')
h+='<pre>'+esc(d.diff).split('\\n').map(l=>l[0]=='+'?`<span class=add>${l}</span>`:l[0]=='-'?`<span class=del>${l}</span>`
:l).join('\\n')+'</pre>';if(d.kind=='binary')h+='<p class=diff>Different binary file'+(d.problem?' ('+esc(d.problem)+')':'')+
'.</p>';if(d.kind=='table'){h+=`<p>${esc(d.layout)}<br><span class=del>Rows only in the first: ${d.only_a}</span><br><span
class=add>Rows only in the second: ${d.only_b}</span><br><span class=diff>Rows in both but changed: ${d.changed??
'(layouts differ)'}</span></p>`;if(d.fields.length)h+='<p>Changed fields (index: rows): '+d.fields.map(f=>f[0]+': '+f[1]).
join(', ')+'</p>';if(d.only_a_ids&&d.only_a_ids.length)h+='<p class=del>First ids only in the first: '+d.only_a_ids.join(
', ')+'</p>';if(d.only_b_ids&&d.only_b_ids.length)h+='<p class=add>First ids only in the second: '+d.only_b_ids.join(', ')+
'</p>';if(d.rows.length)h+='<table><tr><th>id</th><th>field</th><th>first</th><th>second</th></tr>'+d.rows.map(r=>r.fields.
map((f,k)=>`<tr>${k?'':`<td rowspan=${r.fields.length}>${r.id}</td>`}<td>${f[0]}</td><td>${esc(f[1])}</td><td>${esc(f[2])}
</td></tr>`).join('')).join('')+'</table>'}$('out').innerHTML=h}
async function mergeChecked(){const names=[...document.querySelectorAll('#list input:checked')].map(x=>x.dataset.n);
if(!names.length){alert('Tick the DBC tables to merge first.');return}const target=$('target').value;if(!confirm('Merge '+names.length+' table(s) into Data\\\\'+target+
'? The newest copy wins every row it has; rows only older copies have are added. An existing patch keeps its other files'+
' and a .bak copy is made. Close the game first.'))return;$('status').textContent='Merging...';const r=await (await fetch(
'/api/merge',{method:'POST',body:JSON.stringify({names,target})})).json();$('detail').innerHTML='<h2>Merge result</h2>'+r.map(x=>`<p><b>${esc(x.name)}</b> `+(x.error?`<span class=
del>${esc(x.error)}</span>`:x.written?`written to ${esc(x.written)} (${x.files} file(s))`+(x.later.length?`<br><span class=del>Warning: `+
`${esc(x.later.join(', '))} load(s) later and still override it in the game.</span>`:''):`newest copy ${esc(x.winner)};
rows added: ${Object.entries(x.added).map(e=>esc(e[0])+' '+e[1]).join(', ')||'none'}; rows where the newest copy differs
and wins: ${x.kept_from_winner}${x.skipped.length?'; skipped '+esc(x.skipped.join(', ')):''}`)+'</p>').join('');
$('status').textContent='Merged.'}
poll();</script></body></html>""".replace("%MERGED%", MERGED_NAME)


EXPORT_FOLDER = Path(__file__).resolve().parent / "export"


def save_copy(catalog, name, label):
    data = catalog.copy(name, label)
    target = EXPORT_FOLDER / Path(label).stem / name.replace("\\", "/")
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(data or b"")
    return str(target)


def handler_for(catalog):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def reply(self, body, kind="application/json"):
            data = body.encode("utf-8") if isinstance(body, str) else body
            self.send_response(200)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            with catalog:
                self.answer_get()

        def answer_get(self):
            url = urlparse(self.path)
            query = {key: values[0] for key, values in parse_qs(url.query).items()}
            if url.path == "/":
                return self.reply(PAGE, "text/html; charset=utf-8")
            if url.path == "/api/status":
                return self.reply(json.dumps({"progress": catalog.progress, "done": catalog.done,
                                              "problems": catalog.problems}))
            if url.path == "/api/targets":
                return self.reply(json.dumps(merge_targets(catalog)))
            if url.path == "/api/dupes":
                return self.reply(json.dumps(catalog.duplicates(query.get("q", ""), query.get("custom") == "1",
                                                                query.get("differ") == "1")))
            if url.path == "/api/save":
                name = query["name"]
                return self.reply(json.dumps([save_copy(catalog, name, query[side]) for side in ("a", "b")]))
            if url.path == "/api/diff":
                name = query["name"]
                return self.reply(json.dumps(difference(name, catalog.copy(name, query["a"]),
                                                        catalog.copy(name, query["b"]))))
            self.send_error(404)

        def do_POST(self):
            if urlparse(self.path).path != "/api/merge":
                return self.send_error(404)
            request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            with catalog:
                self.reply(json.dumps(merge(catalog, request["names"], request["target"])))

    return Handler


def main(data_folder, port="8093"):
    catalog = Catalog(data_folder)
    threading.Thread(target=catalog.scan, daemon=True).start()
    server = ThreadingHTTPServer(("127.0.0.1", int(port)), handler_for(catalog))
    address = f"http://127.0.0.1:{port}/"
    print(f"MPQ-DupeChecker runs at {address} - close this window to stop it.")
    webbrowser.open(address)
    server.serve_forever()


if __name__ == "__main__":
    main(*sys.argv[1:])
