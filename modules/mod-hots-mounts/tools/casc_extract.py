import argparse
import ctypes as C
import json
import re
from pathlib import Path


class FindData(C.Structure):
    _fields_=[('name',C.c_char*260),('ckey',C.c_ubyte*16),('ekey',C.c_ubyte*16),
              ('tags',C.c_uint64),('size',C.c_uint64),('plain',C.c_void_p),
              ('id',C.c_uint32),('locale',C.c_uint32),('content',C.c_uint32),
              ('spans',C.c_uint32),('available',C.c_uint32),('name_type',C.c_int)]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--dll',required=True);p.add_argument('--storage',required=True)
    p.add_argument('--pattern',required=True);p.add_argument('--output',required=True)
    p.add_argument('--extract',action='store_true');a=p.parse_args()
    output=Path(a.output).resolve()
    if output.exists():raise ValueError('Use a new output directory')
    output.mkdir(parents=True)
    lib=C.WinDLL(str(Path(a.dll).resolve()),use_last_error=True)
    signatures={
        'CascOpenStorage':(C.c_bool,[C.c_char_p,C.c_uint32,C.POINTER(C.c_void_p)]),
        'CascFindFirstFile':(C.c_void_p,[C.c_void_p,C.c_char_p,C.POINTER(FindData),C.c_char_p]),
        'CascFindNextFile':(C.c_bool,[C.c_void_p,C.POINTER(FindData)]),
        'CascFindClose':(C.c_bool,[C.c_void_p]),'CascCloseStorage':(C.c_bool,[C.c_void_p]),
        'CascOpenFile':(C.c_bool,[C.c_void_p,C.c_char_p,C.c_uint32,C.c_uint32,C.POINTER(C.c_void_p)]),
        'CascGetFileSize64':(C.c_bool,[C.c_void_p,C.POINTER(C.c_uint64)]),
        'CascReadFile':(C.c_bool,[C.c_void_p,C.c_void_p,C.c_uint32,C.POINTER(C.c_uint32)]),
        'CascCloseFile':(C.c_bool,[C.c_void_p])}
    for name,(restype,args) in signatures.items():
        f=getattr(lib,name);f.restype=restype;f.argtypes=args
    storage=C.c_void_p()
    lib.GetCascError.restype=C.c_uint32
    callback_type=C.WINFUNCTYPE(C.c_bool,C.c_void_p,C.c_uint32,C.c_char_p,C.c_uint32,C.c_uint32)
    @callback_type
    def progress(user,kind,name,current,total):
        if current==0:print('CASC',kind,name.decode(errors='replace') if name else '',flush=True)
        return False
    class OpenArgs(C.Structure):
        _fields_=[('size',C.c_size_t),('path',C.c_char_p),('product',C.c_char_p),('region',C.c_char_p),
                  ('progress',callback_type),('progress_user',C.c_void_p),('product_cb',C.c_void_p),('product_user',C.c_void_p),
                  ('locale',C.c_uint32),('flags',C.c_uint32),('build',C.c_char_p),('cdn',C.c_char_p)]
    args=OpenArgs();args.size=C.sizeof(args);args.progress=progress;args.locale=2
    lib.CascOpenStorageEx.argtypes=[C.c_char_p,C.POINTER(OpenArgs),C.c_bool,C.POINTER(C.c_void_p)]
    lib.CascOpenStorageEx.restype=C.c_bool
    if not lib.CascOpenStorageEx(str(Path(a.storage).resolve()).encode(),C.byref(args),False,C.byref(storage)):
        raise OSError(lib.GetCascError(),'Unable to open local CASC')
    report=[];find=None
    try:
        data=FindData();find=lib.CascFindFirstFile(storage,b'*',C.byref(data),None)
        if not find or find==C.c_void_p(-1).value:raise OSError(C.get_last_error(),'No CASC files')
        while True:
            name=data.name.decode('utf-8',errors='replace').replace('\\','/')
            if re.search(a.pattern,name,re.I):
                row=dict(path=name,bytes=data.size,local=bool(data.available&1))
                if a.extract:
                    relative=Path(name)
                    if relative.is_absolute() or '..' in relative.parts or ':' in name:raise ValueError('Unsafe CASC member')
                    fh=C.c_void_p()
                    if not lib.CascOpenFile(storage,data.name,2,0,C.byref(fh)):
                        row['error']='Open failed: '+str(C.get_last_error())
                    else:
                        try:
                            size=C.c_uint64()
                            if not lib.CascGetFileSize64(fh,C.byref(size)):raise OSError(C.get_last_error(),'Size failed')
                            if size.value>256*1024*1024:raise ValueError('File exceeds extraction limit')
                            buffer=C.create_string_buffer(size.value);read=C.c_uint32()
                            if not lib.CascReadFile(fh,buffer,size.value,C.byref(read)) or read.value!=size.value:
                                raise OSError(C.get_last_error(),'Incomplete CASC read')
                            target=output/relative;target.parent.mkdir(parents=True,exist_ok=True)
                            with target.open('xb') as stream:stream.write(buffer.raw)
                            row['extracted']=True
                        finally:lib.CascCloseFile(fh)
                report.append(row)
            if not lib.CascFindNextFile(find,C.byref(data)):break
    finally:
        if find and find!=C.c_void_p(-1).value:lib.CascFindClose(find)
        lib.CascCloseStorage(storage)
        (output/'manifest.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(dict(matches=len(report),extracted=sum(r.get('extracted',False) for r in report),manifest=str(output/'manifest.json'))))


if __name__=='__main__':main()
