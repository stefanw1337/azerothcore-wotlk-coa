import argparse
import gzip
import json
import re
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

SCHEMAS = {"auth": "acore_auth", "characters": "acore_characters", "world": "acore_world"}
LEDGER = "coa_manager_migrations"
LEDGER_SCHEMA = {"auth": "acore_auth", "characters": "acore_world", "world": "acore_world"}
AUTH_NAMES = {"auth", "coa-auth", "wildcard-auth", "reborn-auth"}
UNKNOWN_SHA = "0" * 64
INTERRUPTED = "Interrupted SQL must be recovered before replay."
SAFE_ID = re.compile(r"[A-Za-z0-9_.\-]+")
HIDDEN = getattr(subprocess, "CREATE_NO_WINDOW", 0)
GIT_CANDIDATES = ("git", r"C:\Program Files\Git\cmd\git.exe", r"C:\Program Files (x86)\Git\cmd\git.exe")


@dataclass
class Update:
    kind: str
    id: str
    relative: str
    path: Path

    @property
    def key(self):
        return self.kind, self.id.lower()


class Log:
    def __init__(self, path):
        path.parent.mkdir(parents=True, exist_ok=True)
        self.file = path.open("a", encoding="utf-8")

    def __call__(self, text, show=True):
        if show:
            print(text, flush=True)
        self.file.write(datetime.now().strftime("%Y-%m-%d %H:%M:%S ") + text + "\n")
        self.file.flush()


class Server:
    def __init__(self, root):
        self.root = root
        self.mysql = root / "mysql/bin/mysql.exe"
        self.mysqldump = root / "mysql/bin/mysqldump.exe"
        self.login = "--defaults-file=" + str(root / "mysql/admin-client.ini")
        self.manage = root / "Scripts/manage.py"
        for needed in (self.mysql, root / "mysql/admin-client.ini", self.manage):
            if not needed.exists():
                raise RuntimeError(f"{needed} is missing; --server must be the CoA server folder.")

    def client(self, *arguments):
        return [str(self.mysql), self.login, "--default-character-set=utf8mb4", *arguments]

    def query(self, sql):
        result = subprocess.run(self.client("--batch", "--skip-column-names"), input=sql.encode("utf-8"),
                                capture_output=True, creationflags=HIDDEN)
        if result.returncode:
            raise RuntimeError("MySQL: " + result.stderr.decode("utf-8", "replace").strip())
        return result.stdout.decode("utf-8", "replace")

    def run_file(self, schema, path):
        with path.open("rb") as source:
            result = subprocess.run(self.client("--init-command=SET NAMES utf8mb4 COLLATE utf8mb4_unicode_ci",
                                                "--max-allowed-packet=128M", schema),
                                    stdin=source, capture_output=True, creationflags=HIDDEN)
        return result.returncode, result.stderr.decode("utf-8", "replace").strip()

    def launcher(self, action):
        return subprocess.run([sys.executable, str(self.manage), action], capture_output=True, text=True)

    def world_running(self):
        status = self.launcher("status").stdout
        return any(line.split()[:2] == ["world", "running"] for line in status.splitlines() if line.strip())

    def start_mysql(self):
        result = self.launcher("start-mysql")
        if result.returncode:
            raise RuntimeError("MySQL did not start: " + (result.stderr or result.stdout).strip())

    def tables(self):
        rows = self.query("SELECT table_schema,table_name FROM information_schema.tables WHERE table_schema IN "
                          "('acore_auth','acore_characters','acore_world') AND table_name IN ('updates','"
                          + LEDGER + "');")
        return {tuple(line.split("\t")) for line in rows.splitlines() if line}

    def prepare_ledger(self):
        for schema in sorted(set(LEDGER_SCHEMA.values())):
            self.query(f"CREATE TABLE IF NOT EXISTS `{schema}`.`{LEDGER}` (`db` VARCHAR(32) NOT NULL, "
                       "`id` VARCHAR(190) NOT NULL, `sha256` CHAR(64) NOT NULL, `status` VARCHAR(16) NOT NULL, "
                       "`applied_at` DATETIME NOT NULL, `error` TEXT NULL, `baseline` TINYINT NOT NULL DEFAULT 0, "
                       "PRIMARY KEY (`db`,`id`)) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;")

    def history(self):
        tables = self.tables()
        rows = []
        for kind, schema in SCHEMAS.items():
            if (schema, "updates") in tables:
                names = self.query(f"SELECT name FROM `{schema}`.`updates` WHERE name LIKE '%.sql';")
                rows += [(kind, name[:-4], "applied") for name in names.splitlines() if name]
        for schema in sorted(set(LEDGER_SCHEMA.values())):
            if (schema, LEDGER) in tables:
                lines = self.query(f"SELECT `db`,`id`,`status` FROM `{schema}`.`{LEDGER}`;")
                rows += [tuple(line.split("\t")[:3]) for line in lines.splitlines() if line.count("\t") >= 2]
        applied, unfinished = set(), {}
        for db, migration, status in rows:
            key = ("auth" if db in AUTH_NAMES else db, migration.lower())
            if status == "applied":
                applied.add(key)
            else:
                unfinished[key] = status
        return applied, {key: status for key, status in unfinished.items() if key not in applied}

    @staticmethod
    def entry(update, status, error=None):
        text = "NULL" if error is None else "UNHEX('" + error[:8000].encode("utf-8").hex() + "')"
        return (f"REPLACE INTO `{LEDGER_SCHEMA[update.kind]}`.`{LEDGER}` "
                "(`db`,`id`,`sha256`,`status`,`applied_at`,`error`,`baseline`) VALUES "
                f"('{update.kind}','{update.id}','{UNKNOWN_SHA}','{status}',NOW(),{text},0);")

    def backup(self, folder, log):
        if not self.mysqldump.exists():
            raise RuntimeError(f"{self.mysqldump} is missing; run again with --no-backup to skip the backup.")
        folder.mkdir(parents=True, exist_ok=True)
        for schema in SCHEMAS.values():
            started = time.monotonic()
            target = folder / f"{schema}.sql.gz"
            with gzip.open(target, "wb", compresslevel=1) as out, (folder / "mysqldump-errors.log").open("ab") as err:
                child = subprocess.Popen([str(self.mysqldump), self.login, "--default-character-set=utf8mb4",
                                          "--single-transaction", "--routines", "--triggers", "--events",
                                          "--hex-blob", "--max-allowed-packet=512M", "--add-drop-database",
                                          "--databases", schema],
                                         stdout=subprocess.PIPE, stderr=err, creationflags=HIDDEN)
                shutil.copyfileobj(child.stdout, out, 1 << 20)
                if child.wait():
                    raise RuntimeError(f"Backup of {schema} failed; see {folder / 'mysqldump-errors.log'}.")
            log(f"  {schema}: {target.stat().st_size / 1048576:.1f} MB in {time.monotonic() - started:.0f} s")

    def restore(self, folder, log):
        for schema in SCHEMAS.values():
            source = folder / f"{schema}.sql.gz"
            if not source.exists():
                raise RuntimeError(f"{source} is missing.")
            started = time.monotonic()
            child = subprocess.Popen(self.client("--max-allowed-packet=512M"), stdin=subprocess.PIPE,
                                     stderr=subprocess.PIPE, creationflags=HIDDEN)
            with gzip.open(source, "rb") as dump:
                shutil.copyfileobj(dump, child.stdin, 1 << 20)
            child.stdin.close()
            error = child.stderr.read().decode("utf-8", "replace").strip()
            if child.wait():
                raise RuntimeError(f"Restoring {schema} failed: {error}")
            log(f"  {schema} restored in {time.monotonic() - started:.0f} s")


class Source:
    def __init__(self, folder):
        self.folder = folder
        self.git = next((candidate for candidate in GIT_CANDIDATES if shutil.which(candidate)), None)
        if not self.git:
            raise RuntimeError("git was not found; install Git for Windows.")
        if not (folder / "data/sql/updates").is_dir():
            raise RuntimeError(f"{folder} is not a server source checkout.")

    def run(self, *arguments):
        result = subprocess.run([self.git, "-C", str(self.folder), *arguments], capture_output=True,
                                creationflags=HIDDEN)
        if result.returncode:
            raise RuntimeError("git " + " ".join(arguments) + ": " + result.stderr.decode("utf-8", "replace"))
        return result.stdout.decode("utf-8", "replace")

    def head(self):
        return self.run("log", "-1", "--format=%h %s").strip()

    def updates(self):
        found = []
        for kind in SCHEMAS:
            for folder in (f"db_{kind}", f"pending_db_{kind}"):
                directory = self.folder / "data/sql/updates" / folder
                for path in sorted(directory.glob("*.sql")) if directory.is_dir() else []:
                    found.append(Update(kind, path.stem, f"data/sql/updates/{folder}/{path.name}", path))
        rank = {}
        added = self.run("log", "--reverse", "--diff-filter=A", "--name-only", "--pretty=format:", "--",
                         "data/sql/updates", "modules")
        for line in (line.strip() for line in added.splitlines()):
            if line.endswith(".sql"):
                rank.setdefault(line, len(rank))
        return sorted(found, key=lambda update: (rank.get(update.relative, len(rank)), update.relative))

    def keys_at(self, commit):
        if subprocess.run([self.git, "-C", str(self.folder), "cat-file", "-e", commit + "^{commit}"],
                          capture_output=True, creationflags=HIDDEN).returncode:
            raise RuntimeError(f"The source checkout does not contain the database's release commit {commit}.")
        keys = set()
        for line in self.run("ls-tree", "-r", "--name-only", commit, "--", "data/sql/updates").splitlines():
            parts = line.strip().split("/")
            if len(parts) == 5 and parts[4].endswith(".sql"):
                keys.add((parts[3].replace("pending_db_", "").replace("db_", ""), parts[4][:-4].lower()))
        return keys


def release_commit(root):
    provenance = root / "Scripts/binary-provenance.json"
    if not provenance.exists():
        raise RuntimeError(f"{provenance} is missing; pass the database's release commit with --base.")
    return json.loads(provenance.read_text(encoding="utf-8-sig"))["sourceCore"]


def plan(server, source, base, log):
    updates = source.updates()
    unsafe = [update.relative for update in updates if not SAFE_ID.fullmatch(update.id)]
    if unsafe:
        raise RuntimeError("Unsupported SQL file names: " + ", ".join(unsafe))
    server.prepare_ledger()
    applied, unfinished = server.history()
    released = source.keys_at(base)
    missing = [update for update in updates if update.key not in applied]
    older = [update for update in missing if update.key in released]
    todo = [update for update in missing if update.key not in released]
    log(f"Source: {source.head()}")
    log(f"Database release: {base[:12]}")
    log(f"SQL files in source: {len(updates)}, already in the database: {len(updates) - len(missing)}, "
        f"to apply: {len(todo)}")
    if older:
        log(f"Not applied but part of the database's own release (left alone): {len(older)}")
        for update in older[:20]:
            log("  " + update.relative, show=False)
    for update in todo:
        if update.key in unfinished:
            log(f"Retrying {update.relative} (it was {unfinished[update.key]} before).")
    return todo


def apply(server, todo, log):
    started = time.monotonic()
    finished = ""
    for number, update in enumerate(todo, 1):
        began = time.monotonic()
        server.query(finished + server.entry(update, "running", INTERRUPTED))
        code, error = server.run_file(SCHEMAS[update.kind], update.path)
        if code:
            error = error or f"mysql exited with {code}"
            server.query(server.entry(update, "failed", error))
            log(f"[{number}/{len(todo)}] {update.kind:10} {update.id} FAILED")
            log(error)
            return False
        finished = server.entry(update, "applied")
        log(f"[{number}/{len(todo)}] {update.kind:10} {update.id} ok ({time.monotonic() - began:.1f} s)",
            show=number == len(todo) or number % 25 == 0 or time.monotonic() - began > 5)
    server.query(finished)
    log(f"Applied {len(todo)} SQL files in {time.monotonic() - started:.0f} s.")
    return True


def main():
    parser = argparse.ArgumentParser(description="Applies the CoA SQL updates a locally built worldserver needs.")
    parser.add_argument("--server", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--source", type=Path, help="Git checkout the worldserver was built from.")
    parser.add_argument("--base", help="Release commit of the database (default: Scripts/binary-provenance.json).")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--no-backup", action="store_true")
    parser.add_argument("--restore", type=Path, help="Backup folder to restore instead of applying anything.")
    args = parser.parse_args()
    root = args.server.resolve()
    server = Server(root)
    log = Log(root / "Core/Logs/apply-coa-sql.log")
    if server.world_running():
        raise RuntimeError("The world server is running. Stop it first (Stop-CoA.bat).")
    server.start_mysql()
    if args.restore:
        log(f"Restoring the databases from {args.restore} ...")
        server.restore(args.restore, log)
        log("Restore finished.")
        return 0
    if not args.source:
        raise RuntimeError("--source is required.")
    todo = plan(server, Source(args.source.resolve()), args.base or release_commit(root), log)
    if not todo:
        log("The database already has every SQL update this source needs.")
        return 0
    if args.dry_run:
        for update in todo:
            log("  " + update.relative)
        return 0
    if not args.no_backup:
        folder = root / "Backups" / datetime.now().strftime("before-sql-%Y%m%d-%H%M%S")
        log(f"Backing up the databases to {folder} ...")
        server.backup(folder, log)
    if not apply(server, todo, log):
        log("Stopped at the failed file. Nothing after it was applied. Send Core\\Logs\\apply-coa-sql.log.")
        if not args.no_backup:
            log(f"The backup from before this run is in {folder}.")
        return 1
    return 0


if __name__ == "__main__":
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(errors="replace")
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        print("ERROR: " + str(error), file=sys.stderr, flush=True)
        sys.exit(1)
