import io
import sys
import urllib.request
import zipfile
from pathlib import Path

URL = 'https://github.com/Solstice245/m3studio/archive/refs/heads/master.zip'
TARGET = Path(__file__).resolve().parent / 'm3studio'


def main():
    if (TARGET / 'io_m3.py').exists():
        print(f'm3studio already present: {TARGET}')
        return 0
    print(f'Downloading {URL} ...', flush=True)
    data = urllib.request.urlopen(URL, timeout=120).read()
    print(f'  {len(data) // 1024} KB, unpacking', flush=True)
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        root = z.namelist()[0].split('/')[0]
        for name in z.namelist():
            rel = name[len(root) + 1:]
            if not rel or name.endswith('/'):
                continue
            out = TARGET / rel
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(z.read(name))
    print(f'm3studio installed: {TARGET}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
