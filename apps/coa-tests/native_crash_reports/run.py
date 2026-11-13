from pathlib import Path
import os
import shutil
import subprocess
import tempfile

here = Path(__file__).resolve().parent
build = Path(os.environ['COA_TEST_BUILD_DIR'])
compiler = Path(os.environ['VCToolsInstallDir']) / 'bin/Hostx64/x64/cl.exe'
handler = build / 'src/server/apps/CMakeFiles/worldserver.dir/__/__/common/Debugging/WheatyExceptionReport.cpp.obj'
revision = build / 'src/common/CMakeFiles/common.dir/GitRevision.cpp.obj'
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    core = root / 'Core'
    core.mkdir()
    target = core / 'crash-probe.exe'
    subprocess.run([str(compiler), '/nologo', '/MD', '/EHsc', str(here / 'cases.cpp'), str(handler), str(revision),
                    str(build / 'src/server/apps/CMakeFiles/worldserver.dir/cmake_pch.cxx.obj'),
                    str(build / 'src/common/CMakeFiles/common.dir/cmake_pch.cxx.obj'),
                    '/Fe:' + str(target), '/Fo:' + str(root / 'probe.obj'), '/link', 'dbghelp.lib', 'version.lib', 'advapi32.lib'], check=True)
    child = subprocess.run([str(target)], cwd=root, creationflags=subprocess.CREATE_NO_WINDOW, timeout=90)
    assert child.returncode != 0
    reports = list((core / 'Crashes').glob('*.txt'))
    dumps = list((core / 'Crashes').glob('*.dmp'))
    assert len(reports) == len(dumps) == 1
    assert 'Exception code: C0000005' in reports[0].read_text(errors='replace')
    assert dumps[0].stat().st_size > 0
    assert not (root / 'Crashes').exists()
    if evidence := os.environ.get('COA_CRASH_REPORT_EVIDENCE'):
        evidence = Path(evidence)
        assert evidence.name.startswith('crash-report-evidence-') and not evidence.exists()
        shutil.copytree(core / 'Crashes', evidence / 'Core/Crashes')
        (evidence / '.crash-report-evidence').write_text('native exception handler output')
    print('PASS: actual worldserver exception handler created missing Core/Crashes and wrote text plus minidump beside its executable, independently of working directory')
