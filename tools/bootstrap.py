"""Fetch pinned build-only dependencies into .deps; no game paths are used."""
import hashlib, io, json, pathlib, urllib.request, zipfile
ROOT = pathlib.Path(__file__).resolve().parents[1]
def download(url):
    return urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent':'D2RR-ItemDatabase'}), timeout=60).read()
def main():
    lock=json.loads((ROOT/'upstream.lock.json').read_text())
    sdk=ROOT/'.deps/sdk'
    if not (sdk/'include/D2RLPlugin/api.h').exists():
        data=download(f"https://api.github.com/repos/D2RLoader/PluginSDK/zipball/{lock['sdk']['sha']}")
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            for info in z.infolist():
                parts=pathlib.PurePosixPath(info.filename).parts[1:]
                if not parts or info.is_dir(): continue
                dest=sdk.joinpath(*parts).resolve()
                if not dest.is_relative_to(sdk.resolve()): raise ValueError('unsafe archive path')
                dest.parent.mkdir(parents=True,exist_ok=True)
                dest.write_bytes(z.read(info))
    header=ROOT/'.deps/json/nlohmann/json.hpp'
    if not header.exists():
        data=download('https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp')
        if hashlib.sha256(data).hexdigest()!=lock['json']['sha256']: raise ValueError('JSON header checksum mismatch')
        header.parent.mkdir(parents=True,exist_ok=True)
        header.write_bytes(data)
    if hashlib.sha256(header.read_bytes()).hexdigest()!=lock['json']['sha256']: raise ValueError('JSON header checksum mismatch')
    print('Pinned build dependencies ready in .deps')
if __name__=='__main__': main()
