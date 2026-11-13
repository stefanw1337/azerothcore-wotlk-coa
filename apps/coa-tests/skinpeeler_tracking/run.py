from pathlib import Path
import os
import shutil
import subprocess
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[2]
include = root / 'modules/mod-skinpeelerquest/src'
with tempfile.TemporaryDirectory() as directory:
    target = Path(directory) / ('tracking.exe' if os.name == 'nt' else 'tracking')
    if os.name == 'nt' and shutil.which('cl'):
        command = ['cl', '/nologo', '/std:c++20', '/EHsc', '/W4', '/WX', '/I' + str(include),
                   str(here / 'cases.cpp'), '/Fe:' + str(target), '/Fo:' + str(Path(directory) / 'tracking.obj')]
    else:
        command = ['g++', '-std=c++20', '-pthread', '-Wall', '-Wextra', '-Werror', '-I' + str(include),
                   str(here / 'cases.cpp'), '-o', str(target)]
    subprocess.run(command, check=True)
    subprocess.run([str(target)], check=True)
print('160000 cross-thread notification and independent player state checks passed')
