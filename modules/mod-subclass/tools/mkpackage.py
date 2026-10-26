"""Assemble the source-side part of the subclass test package.
    python mkpackage.py <repo> <carried server tables> <database release commit> <out folder> <LiveMon.exe>
Writes <out folder>/CoA-Subclass with the installer, configs, SQL (CoA updates newer than the release commit plus the
module's own), server tables and LiveMon. Make-Package.ps1 on the build PC adds worldserver.exe, the client patches
and the SpellActivationOverlay addon."""
import json
import shutil
import subprocess
import sys
from datetime import date
from pathlib import Path

TEXT_FOR_WINDOWS = ("Install-Subclass.bat", "README.txt")


def windows_text(source, target):
    target.write_bytes(source.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))


def main(repo, tables, base, out, livemon):
    repo, tables, out = Path(repo).resolve(), Path(tables).resolve(), Path(out).resolve()
    module = repo / "modules" / "mod-subclass"
    package = out / "CoA-Subclass"
    if package.exists():
        shutil.rmtree(package)
    files = package / "files"
    (files / "tools").mkdir(parents=True)
    for name in ("install.py",) + TEXT_FOR_WINDOWS:
        source = module / "package" / name
        if name in TEXT_FOR_WINDOWS:
            windows_text(source, package / name)
        else:
            shutil.copy2(source, package / name)
    shutil.copy2(module / "tools" / "apply_coa_sql.py", files / "tools" / "apply_coa_sql.py")
    (files / "configs").mkdir()
    for dist in (module / "conf" / "mod_subclass.conf.dist", repo / "modules" / "mod-livemon" / "conf" /
                 "mod_livemon.conf.dist"):
        shutil.copy2(dist, files / "configs" / dist.name)
    subprocess.run([sys.executable, str(module / "tools" / "apply_coa_sql.py"), "--source", str(repo), "--base", base,
                    "--export", str(files / "sql" / "core")], check=True)
    for kind, folders in (("world", [module / "data/sql/db-world"]),
                          ("characters", [module / "data/sql/db-characters",
                                          repo / "modules/mod-livemon/data/sql/db-characters"])):
        target = files / "sql" / kind
        target.mkdir(parents=True)
        for folder in folders:
            for sql in sorted(folder.glob("*.sql")):
                shutil.copy2(sql, target / sql.name)
    shutil.copytree(tables, files / "dbc_races")
    shutil.copytree(module / "addon" / "Ascension_Subclass", files / "addons" / "Ascension_Subclass",
                    ignore=shutil.ignore_patterns("__pycache__"))
    (files / "LiveMon").mkdir()
    shutil.copy2(livemon, files / "LiveMon" / "LiveMon.exe")
    (files / "LiveMon" / "livemon.ini").write_bytes(b"# LiveMon window settings\r\nServer = http://127.0.0.1:8089\r\n")
    head = subprocess.run(["git", "-C", str(repo), "log", "-1", "--format=%h"], capture_output=True, text=True,
                          check=True).stdout.strip()
    (package / "package.json").write_text(json.dumps({"built": date.today().isoformat(), "core": head,
                                                      "databaseRelease": base, "coaCustom": "1.9.2"}, indent=1),
                                          encoding="utf-8")
    print(f"Assembled {package}")


if __name__ == "__main__":
    main(*sys.argv[1:6])
