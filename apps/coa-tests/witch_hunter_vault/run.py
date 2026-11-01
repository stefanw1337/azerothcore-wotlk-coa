CLI_DESCRIPTION = """Compile the actual Vault visual choice: forward and sideways dashes handspring, backward dashes flip back."""

import argparse
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from source_paths import git_source  # noqa: E402


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
method = runpy.run_path(str(HERE.parent / "client_compat/run.py"))["method"]


def main():
    parser = argparse.ArgumentParser(description=CLI_DESCRIPTION)
    parser.add_argument("--source-ref", help="Read the Vault code from a local Git ref.")
    args = parser.parse_args()
    path = "src/server/coa/AscensionWitchHunterAbilities.cpp"
    source = (git_source(["git", "show", f"{args.source_ref}:{path}"], cwd=ROOT).decode()
              if args.source_ref else ROOT.joinpath(path).read_text())
    code = "#include <cassert>\n#include <cstdint>\nusing uint32=std::uint32_t;\n"
    code += method(source, "enum WitchHunterVaultKits") + ";\n"
    code += method(source, "uint32 VaultKit(") + "\n"
    code += r"""
int main()
{
    assert(VaultKit(1.0f)==KIT_VAULT_HANDSPRING);
    assert(VaultKit(0.0f)==KIT_VAULT_HANDSPRING);
    assert(VaultKit(-1.0f)==KIT_VAULT_BACK_HANDSPRING);
    assert(KIT_VAULT_HANDSPRING!=KIT_VAULT_BACK_HANDSPRING);
}
"""
    vc_tools = os.environ.get("VCToolsInstallDir")
    compiler = (str(Path(vc_tools) / "bin/Hostx64/x64/cl.exe") if vc_tools else
                shutil.which(os.environ.get("CXX", "cl.exe" if os.name == "nt" else "c++")))
    if not compiler:
        raise RuntimeError("Enable a C++20 compiler (VS Developer PowerShell on Windows).")
    with tempfile.TemporaryDirectory(prefix="coa-witch-hunter-vault-") as directory:
        out = Path(directory)
        cpp = out / "vault.cpp"
        cpp.write_text(code, encoding="utf-8")
        executable = out / ("vault.exe" if os.name == "nt" else "vault")
        if Path(compiler).stem.lower() == "cl":
            flags = ["/nologo", "/std:c++20", "/EHsc", "/W4", "/WX", "/utf-8", str(cpp), "/Fe" + str(executable)]
        else:
            flags = ["-std=c++20", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(executable)]
        subprocess.run([compiler, *flags], cwd=out, check=True, timeout=60)
        subprocess.run([str(executable)], cwd=out, check=True, timeout=15)
    print("PASS: forward and sideways Vaults handspring, backward Vaults flip back")


if __name__ == "__main__":
    main()
