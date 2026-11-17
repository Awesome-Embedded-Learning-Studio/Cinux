#!/usr/bin/env python3
"""Prepare the source-only Cinux development overlay for Buildroot."""
import argparse
import pathlib
import re
import shutil
import subprocess
import sysconfig


def copy_file(source, root, target=None):
    source = pathlib.Path(source)
    destination = root / (target or str(source)).lstrip("/")
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source.resolve(), destination)


def stage_python(root):
    python = pathlib.Path(shutil.which("python3")).resolve()
    copy_file(python, root, "/usr/bin/python3")
    library = pathlib.Path(sysconfig.get_path("stdlib"))
    destination = root / str(library).lstrip("/")
    shutil.copytree(library, destination, dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("site-packages", "__pycache__", "test", "tests", "config-*", "*.a",
                                                  "idlelib", "tkinter", "turtledemo", "ensurepip"))
    programs = [python, *destination.glob("lib-dynload/*.so")]
    for program in programs:
        listing = subprocess.run(["ldd", str(program)], check=True, capture_output=True,
                                 text=True).stdout
        for line in listing.splitlines():
            fields = line.split()
            if len(fields) >= 3 and fields[1] == "=>" and fields[2].startswith("/"):
                copy_file(fields[2], root)


def stage_program(name, root, copy_file):
    program = shutil.which(name)
    if not program:
        raise RuntimeError(f'required native tool missing: {name}')
    copy_file(program, root, f'/usr/bin/{name}')
    listing = subprocess.run(['ldd', program], capture_output=True, text=True, check=True).stdout
    for line in listing.splitlines():
        for field in line.split():
            if field.startswith('/') and pathlib.Path(field).is_file():
                copy_file(field, root)


def merge_overlay(source, target):
    if target.is_symlink():
        raise RuntimeError(f'overlay directory must not be a symlink: {target}')
    target.mkdir(parents=True, exist_ok=True)
    for item in source.iterdir():
        destination = target / item.name
        if item.is_symlink():
            destination.unlink(missing_ok=True)
            destination.symlink_to(item.readlink())
        elif item.is_dir():
            merge_overlay(item, destination)
        else:
            if destination.is_symlink():
                destination.unlink()
            shutil.copy2(item, destination)


def source_ignores(directory, names):
    return [name for name in names if name in ('.git', '.cache', '__pycache__') or
            ((name == 'build' or name.startswith(('build-', 'build_'))) and
             (pathlib.Path(directory) / name).is_dir())]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--overlay-only', action='store_true', help='only generate the BR2_ROOTFS_OVERLAY directory')
    parser.add_argument('--build-dir', type=pathlib.Path, default=pathlib.Path('build'))
    parser.add_argument('--arch', default='x86_64', choices=['x86_64', 'armv7'])
    args = parser.parse_args()
    repo = pathlib.Path(__file__).resolve().parents[3]
    build = args.build_dir.resolve()
    overlay_root = build / 'buildroot/overlay'
    shutil.rmtree(overlay_root, ignore_errors=True)
    root = overlay_root
    (root / 'etc').mkdir(parents=True, exist_ok=True)
    if args.arch == 'armv7':
        # No native compiler rides the armv7 volume: buildroot has no
        # target-side gcc, and the x86 staging binaries could not run in
        # the arm guest anyway. The guest works through the busybox the
        # defconfig builds; the source tree stays for reference. The
        # cross-built fbdev desk joins the volume the same way the x86
        # lane's vesper rides the development volume.
        desk = repo / 'build/armv7-vesper/desk'
        if desk.exists():
            (root / 'bin').mkdir(parents=True, exist_ok=True)
            shutil.copy2(desk, root / 'bin' / 'vesper')
        else:
            raise RuntimeError('desk artifact missing; build the vesper_desk target first')
    else:
        # The overlay root already exists at this point (its etc seat
        # was made above), so merge the staging tree into it instead of
        # demanding a fresh directory.
        shutil.copytree(build / 'toolchain-staging', overlay_root,
                        symlinks=True, dirs_exist_ok=True)
    source = root / 'home/cinux/Cinux'
    source.mkdir(parents=True)
    for name in ('CMakeLists.txt', '.gitmodules', 'kernel', 'base', 'user', 'assets', 'cmake', 'scripts', 'test', 'third_party'):
        item = repo / name
        if item.is_dir():
            shutil.copytree(item, source / name, ignore=source_ignores)
        else:
            shutil.copy2(item, source / name)
    if args.arch == 'armv7':
        merge_overlay(repo / 'user/rootfs/buildroot/overlay', root)
        # The boot ladder's mount check reads this one fixture from the
        # card. Staged after the overlay merge so the 99-byte motd wins
        # over the banner.
        seeds = repo / 'scripts/ext2_seed'
        shutil.copy2(seeds / 'motd.txt', root / 'etc' / 'motd')
        for name in ('zh.txt', 'zh_ascii.txt'):
            shutil.copy2(seeds / name, root / 'etc' / name)
        print(f'[native-build] BR2_ROOTFS_OVERLAY={overlay_root}', flush=True)
        return
    stage_python(root)
    shutil.copytree('/usr/include', root / 'usr/include', dirs_exist_ok=True)
    for name in ('include', 'include-fixed'):
        directory = pathlib.Path(subprocess.run(['gcc', f'-print-file-name={name}'],
                                 capture_output=True, text=True, check=True).stdout.strip())
        if directory.is_absolute() and directory.is_dir():
            shutil.copytree(directory, root / str(directory).lstrip('/'), dirs_exist_ok=True)
    for name in ('cmake', 'ninja', 'make', 'ar', 'ranlib', 'nm', 'objcopy', 'readelf', 'tar', 'dd', 'ping'):
        stage_program(name, root, copy_file)
    (root / 'bin/ping').unlink(missing_ok=True)
    (root / 'bin/ping').symlink_to('/usr/bin/ping')
    information = subprocess.run(['cmake', '--system-information'], capture_output=True,
                                 text=True, check=True).stdout
    cmake_root = pathlib.Path(re.search(r'CMAKE_ROOT "([^"]+)"', information)[1])
    for name in ('Modules', 'Templates'):
        shutil.copytree(cmake_root / name, root / str(cmake_root / name).lstrip('/'), dirs_exist_ok=True)
    shutil.copy2(repo / 'cmake/platform/Platform/Cinux.cmake',
                 root / str(cmake_root / 'Modules/Platform/Cinux.cmake').lstrip('/'))
    subprocess.run(['gcc', '-D_GNU_SOURCE', '-O2', '-Wall', '-Wextra', '-Werror',
                    str(repo / 'scripts/ext2_seed/nativeprobe.c'), '-o', str(root / 'bin/nativeprobe')], check=True)
    for name, target in (('cc', 'gcc'), ('c++', 'g++')):
        (root / 'usr/bin' / name).unlink(missing_ok=True)
        (root / 'usr/bin' / name).symlink_to(target)
    merge_overlay(repo / 'user/rootfs/buildroot/overlay', root)
    copy_file(build / 'glibc/vesper-desk', root, '/bin/vesper')
    print(f'[native-build] BR2_ROOTFS_OVERLAY={overlay_root}', flush=True)


if __name__ == '__main__':
    main()
