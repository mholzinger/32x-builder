#!/usr/bin/env python3
"""Expand real box/wedge corners through the C definitions; update only on change."""
from pathlib import Path
import os,shlex,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='32x-furniture-meshes-') as tmp:
 binary=Path(tmp)/'meshgen'
 subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c11',f'-I{root / "sh_src"}',str(root/'experiments/sh2zam-nightly/generate_meshes.c'),'-o',str(binary)],check=True)
 data=subprocess.check_output([str(binary)])
 target=root/'experiments/sh2zam-nightly/mesh_positions.h'
 if not target.exists() or target.read_bytes()!=data:target.write_bytes(data)
