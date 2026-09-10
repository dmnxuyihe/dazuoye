"""Assemble Qt binaries/dependencies and coherent source; no product acceptance runs."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--linux-only', action='store_true', help='Update the Linux runtime without rebuilding legacy documents/archives')
args = parser.parse_args()

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'deliverables/qt'
LINUX=OUT/'linux'
SDK=ROOT/'.runtime/qt/6.5.3/gcc_64'
DEPS=ROOT/'.runtime/qt-deps/usr/lib/x86_64-linux-gnu'
OUT.mkdir(parents=True,exist_ok=True)
for folder in ['bin','lib','plugins','licenses','libexec','resources']:(LINUX/folder).mkdir(parents=True,exist_ok=True)
for name in ['electra-user','electra-admin']:
    temporary=LINUX/'bin'/(name+'.next')
    shutil.copy2(ROOT/'.runtime/qt-build'/name,temporary)
    temporary.replace(LINUX/'bin'/name)
    (LINUX/'bin'/name).chmod(0o755)
for group,names in {'platforms':['libqxcb.so','libqoffscreen.so','libqminimal.so'],'sqldrivers':['libqsqlite.so'],'tls':['libqopensslbackend.so','libqcertonlybackend.so'],'imageformats':['libqjpeg.so','libqsvg.so','libqgif.so'],'iconengines':['libqsvgicon.so'],'xcbglintegrations':['libqxcb-glx-integration.so','libqxcb-egl-integration.so'],'multimedia':['libffmpegmediaplugin.so']}.items():
    target=LINUX/'plugins'/group;target.mkdir(exist_ok=True)
    for name in names:
        source=SDK/'plugins'/group/name
        if source.exists():
            temporary=target/(name+'.next');shutil.copy2(source,temporary);temporary.replace(target/name)
shutil.copy2(SDK/'libexec/QtWebEngineProcess',LINUX/'libexec/QtWebEngineProcess.next')
(LINUX/'libexec/QtWebEngineProcess.next').replace(LINUX/'libexec/QtWebEngineProcess')
shutil.copytree(SDK/'resources',LINUX/'resources',dirs_exist_ok=True)
shutil.copytree(SDK/'translations/qtwebengine_locales',LINUX/'translations/qtwebengine_locales',dirs_exist_ok=True)
# Follow ELF dependency metadata; retain only Qt SDK and assignment-extracted support libs.
env={**os.environ,'LD_LIBRARY_PATH':f'{SDK}/lib:{DEPS}'}
queue=[LINUX/'libexec/QtWebEngineProcess',LINUX/'bin'/'electra-user',LINUX/'bin'/'electra-admin',*list((LINUX/'plugins').rglob('*.so'))]
seen=set()
while queue:
    binary=queue.pop()
    if binary in seen:continue
    seen.add(binary)
    listing=subprocess.check_output(['ldd',str(binary)],env=env,text=True)
    for line in listing.splitlines():
        if '=> not found' in line:raise RuntimeError(f'Unresolved bundled dependency: {line.strip()}')
        if '=>' not in line:continue
        soname,tail=line.strip().split('=>',1);soname=soname.strip();source=Path(tail.strip().split(' ')[0])
        if str(source).startswith(str(SDK)) or str(source).startswith(str(DEPS)):
            target=LINUX/'lib'/soname
            if not target.exists():shutil.copy2(source,target);queue.append(source)
(LINUX/'bin/qt.conf').write_text('[Paths]\nPrefix=..\nPlugins=plugins\nLibraries=lib\nLibraryExecutables=libexec\nData=.\nTranslations=translations\n')
for role in ['admin','user']:
    script=LINUX/f'start-{role}.sh'
    script.write_text('''#!/usr/bin/env bash
set -euo pipefail
bundle_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$bundle_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$bundle_dir/plugins"
if [[ $# -eq 0 ]]; then set -- --api https://lv-l40s-liuzihang.taild6df1c.ts.net:8443; fi
exec "$bundle_dir/bin/electra-'''+role+'" "$@"\n')
    script.chmod(0o755)
shutil.copytree(ROOT/'desktop/licenses',LINUX/'licenses',dirs_exist_ok=True)
for package in (ROOT/'.runtime/qt-deps/usr/share/doc').glob('*'):
    notice=package/'copyright'
    if notice.is_file():
        dest=LINUX/'licenses/ubuntu'/package.name;dest.mkdir(parents=True,exist_ok=True);shutil.copy2(notice,dest/'copyright')
if args.linux_only:
    print(json.dumps({'linux': str(LINUX), 'qt_version': '6.5.3', 'multimedia_backend': 'ffmpeg'}, ensure_ascii=False))
    raise SystemExit(0)
shutil.copy2(ROOT/'docs/QT_MIGRATION.md',OUT/'使用与架构说明.md')
shutil.copy2(ROOT/'docs/QT_MAP_FIX.md',OUT/'地图修复说明.md')
shutil.copy2(ROOT/'docs/QT_WEB_PARITY.md',OUT/'原生页面重构说明.md')
shutil.copy2(ROOT/'docs/QT_ONLINE_MAP.md',OUT/'在线地图接入说明.md')
shutil.copy2(ROOT/'.runtime/qt-webmap/visual-tests.log',OUT/'原生页面定向测试日志.txt')
shutil.copy2(ROOT/'.runtime/qt-webmap/test-map.log',OUT/'地图测试日志.txt')
for role in ['admin','user']:
    source=ROOT/'.runtime/qt-webmap'/role
    if source.exists():shutil.copytree(source,OUT/'screenshots'/role,dirs_exist_ok=True)
for name in ['答辩汇报-Qt重构版.pptx','答辩汇报-Qt重构版.pdf','答辩讲稿-Qt重构版.docx']:
    shutil.copy2(ROOT/'deliverables/答辩材料'/name,OUT/name)
shutil.copy2(ROOT/'.runtime/qt-dialog-fix/build.log',OUT/'编译日志.txt')
shutil.copy2(ROOT/'.runtime/qt-dialog-fix/tests.log',OUT/'弹窗关闭回归测试.txt')
shutil.copy2(ROOT/'docs/QT_DIALOG_FIX.md',OUT/'弹窗修复说明.md')
for name in ['manager','wallet','detail','form']:
    shutil.copy2(ROOT/'.runtime/qt-dialog-fix'/(name+'.png'),OUT/'screenshots'/(name+'-dialog.png'))
(OUT/'README.txt').write_text('''Electra Qt 桌面端交付 · 2026-09-08 弹窗关闭修复版

1. Ubuntu 22.04 x86_64 图形桌面：运行 linux/start-admin.sh 或 linux/start-user.sh。
   已附 Qt 6.5.3 和额外 XCB 运行依赖；系统仍需桌面环境、glibc、OpenGL 驱动与常规 Ubuntu 基础库。
   默认连接现有公网 API；可传 --api http://127.0.0.1:4173 连接本地服务。
2. 源码：assignment-qt-source.zip。包含 Qt C++ 客户端、现有 FastAPI/Web 源码、预测产物、构建脚本与文档。
3. Windows：源码内 desktop/scripts/build-windows.ps1；本次没有 Windows 可执行文件或运行验收结论。
4. 答辩：Qt 重构版 PPTX / PDF / DOCX。旧压缩包为之前版本，本次以 assignment-qt-delivery.zip 为准。
5. 两端编译成功；原生页面定向测试 5 项和在线地图测试 3 项通过。已生成连接真实服务的 13 页截图。按用户要求未运行完整业务验收，Windows 未验证。
6. Web 公网入口保留：https://lv-l40s-liuzihang.taild6df1c.ts.net:8443/ui/admin.html
7. 恢复本地业务库可使用 database/assignment-database.dump（既有交付快照；不会自动恢复）。恢复说明见源码 DEPLOYMENT.md。

更多范围及限制见 使用与架构说明.md。地图区域采用 Qt WebEngine + Leaflet + OSM 在线底图；其余页面与业务交互保留原生 Qt。Qt Charts 仅用于管理员图表。
''',encoding='utf-8')
source_files=[]
for folder in ['desktop','src','scripts','docs']:
    for p in (ROOT/folder).rglob('*'):
        if p.is_file() and '__pycache__' not in p.parts and p.suffix not in ['.pyc'] and not any(s.startswith('build-') for s in p.relative_to(ROOT).parts[:-1]):source_files.append(p)
for name in ['README.md','ARCHITECTURE.md','DEPLOYMENT.md','pyproject.toml','.env.example']:source_files.append(ROOT/name)
source_zip=OUT/'assignment-qt-source.zip'
with zipfile.ZipFile(source_zip,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for p in sorted(source_files):z.write(p,'assignment/'+str(p.relative_to(ROOT)))
main_zip=ROOT/'deliverables/assignment-qt-delivery.zip'
with zipfile.ZipFile(main_zip,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for p in sorted(OUT.rglob('*')):
        if p.is_file() and p.name!='SHA256SUMS':z.write(p,'Electra-Qt/'+str(p.relative_to(OUT)))
    dump=ROOT/'deliverables/database/assignment-database.dump'
    if dump.exists():z.write(dump,'Electra-Qt/database/assignment-database.dump')
def digest(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()
(OUT/'SHA256SUMS').write_text(digest(main_zip)+'  ../assignment-qt-delivery.zip\n'+digest(source_zip)+'  assignment-qt-source.zip\n')
print(json.dumps({'delivery':str(main_zip),'bytes':main_zip.stat().st_size,'source_files':len(source_files),'qt_version':'6.5.3','acceptance':'skipped as requested'},ensure_ascii=False,indent=2))
