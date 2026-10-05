"""Helpers for the game's archives: .rcc files are plain ZIPs; text tables are AES-256-ECB."""
import os
import subprocess
import sys
import time
import zipfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import export_item_catalog as E  # noqa: E402  (AES key, compbyte tables)


def rewrite_zip(path, replace):
    """Rewrite a zip, replacing/adding entries (name -> bytes), keeping each entry's compression."""
    tmp = path + '.tmp'
    with zipfile.ZipFile(path) as src, zipfile.ZipFile(tmp, 'w') as dst:
        lower = {k.lower(): k for k in replace}
        done = set()
        for info in src.infolist():
            key = lower.get(info.filename.lower())
            data = replace[key] if key else src.read(info)
            if key:
                done.add(key)
            zi = zipfile.ZipInfo(info.filename, info.date_time)
            zi.compress_type = info.compress_type
            zi.external_attr = info.external_attr
            dst.writestr(zi, data)
        ctype = src.infolist()[0].compress_type
        for k, data in replace.items():
            if k not in done:
                zi = zipfile.ZipInfo(k, time.localtime()[:6])
                zi.compress_type = ctype
                dst.writestr(zi, data)
    os.replace(tmp, path)


def aes(data, decrypt):
    args = ['openssl', 'enc', '-aes-256-ecb', '-nopad', '-K', E.AES_KEY] + (['-d'] if decrypt else [])
    return subprocess.run(args, input=data, capture_output=True, check=True).stdout
