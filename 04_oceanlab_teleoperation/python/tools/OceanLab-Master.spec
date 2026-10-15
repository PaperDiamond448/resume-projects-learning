# -*- mode: python ; coding: utf-8 -*-


a = Analysis(
    ['C:\\OceanLab-Python\\program\\main.py'],
    pathex=[],
    binaries=[],
    datas=[('C:\\OceanLab-Python\\program\\resources', 'resources')],
    hiddenimports=['PyQt5', 'matplotlib', 'numpy', 'matlab'],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='OceanLab-Master',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=['C:\\OceanLab-Python\\program\\resources\\master.ico'],
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='OceanLab-Master',
)
