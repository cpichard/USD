#
# Copyright 2020 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
#
import setuptools
import argparse
import glob
import os
import platform
import re
import shutil
import sys

# This setup.py script expects to be run from an inst directory in a typical
# USD build run from build_usd.py.
#
# The output of this setup will be a wheel package that does not work. After
# building the wheel package, you will also need to relocate dynamic library
# dependencies into the package, and you will need to update the pluginfo files
# to point to the new locations of those shared library dependencies. How this
# is done depends on platform, and is mostly accomplished by steps in the CI
# system.

# Define special arguments for setup.py to customize behavior.
parser = argparse.ArgumentParser()
parser.add_argument(
    "--post-release-tag", type=str,
    help="Post release tag to append to version number")

args, remaining = parser.parse_known_args()

# Remove our special arguments from sys.argv so that setuptools doesn't choke on
# them. argparse will also eat the "setup.py" argument in sys.argv[0] which is
# apparently necessary for setuptools, so we manually prepend that to the
# remaining unprocessed arguments.
sys.argv = [sys.argv[0]] + remaining

def windows():
    return platform.system() == "Windows"

WORKING_ROOT = '.'
USD_BUILD_OUTPUT = os.path.join(WORKING_ROOT, 'inst')
BUILD_DIR = os.path.join(WORKING_ROOT, 'pypi')

# Copy everything in lib over before we start making changes
shutil.copytree(os.path.join(USD_BUILD_OUTPUT, 'lib'), os.path.join(BUILD_DIR, 'lib'))

# Move the pluginfos into a directory contained inside pxr, for easier
# distribution. This breaks the relative paths in the pluginfos, but we'll need
# to update them later anyway after running "auditwheel repair", which will
# move the libraries to a new directory
shutil.move(os.path.join(BUILD_DIR, 'lib/usd'), os.path.join(BUILD_DIR, 'lib/python/pxr/pluginfo'))

if windows():
    # On windows we also need dlls from the bin directory
    shutil.copytree(os.path.join(USD_BUILD_OUTPUT, 'bin'), os.path.join(BUILD_DIR, 'bin'))

    # On Linux and Mac there are tools that do this for us (auditwheel and
    # delocate) On Windows we'll move these here in setup. This is simpler
    # because there are no RPATHs to worry about.
    dll_files = glob.glob(os.path.join(BUILD_DIR, "lib/*.dll"))
    dll_files.extend(glob.glob(os.path.join(BUILD_DIR, "bin/*.dll")))
    for f in dll_files:
        shutil.move(f, os.path.join(BUILD_DIR, "lib/python/pxr"))

    # Because there are no RPATHS, patch __init__.py
    # See this thread and related conversations
    # https://mail.python.org/pipermail/distutils-sig/2014-September/024962.html
    with open(os.path.join(BUILD_DIR, 'lib/python/pxr/__init__.py'), 'a+') as init_file:
        init_file.write('''

# appended to this file for the windows PyPI package
import os, sys
dllPath = os.path.split(os.path.realpath(__file__))[0]
if sys.version_info >= (3, 8, 0):
    os.environ['PXR_USD_WINDOWS_DLL_PATH'] = dllPath
# Note that we ALWAYS modify the PATH, even for python-3.8+. This is because:
#    - Anaconda python interpreters are modified to use the old, pre-3.8, PATH-
#      based method of loading dlls
#    - extra calls to os.add_dll_directory won't hurt these anaconda
#      interpreters
#    - similarly, adding the extra PATH entry shouldn't hurt standard python
#      interpreters
#    - there's no canonical/bulletproof way to check for an anaconda interpreter
os.environ['PATH'] = dllPath + os.pathsep + os.environ['PATH']
''')

# Get the readme text
with open("README.md", "r") as fh:
    long_description = fh.read()

# Get the library version number from the installed pxr.h header.
with open(os.path.join(USD_BUILD_OUTPUT, "include/pxr/pxr.h"), "r") as fh:
    for line in fh:
        m = re.match(r"#define PXR_MINOR_VERSION (\d+)", line)
        if m:
            minorVersion = m.groups(1)[0]
            continue

        m = re.match(r"#define PXR_PATCH_VERSION (\d+)", line)
        if m:
            patchVersion = m.groups(1)[0]
            continue

version = "{}.{}".format(minorVersion, patchVersion)

if args.post_release_tag:
    version = "{}.{}".format(version, args.post_release_tag)

# Config
setuptools.setup(
    name="usd-core",
    version=version,
    author="Pixar Animation Studios",
    author_email="openusd+usd_pypi@pixar.com",
    description="Pixar's Universal Scene Description",
    long_description=long_description,
    long_description_content_type="text/markdown",
    license="LicenseRef-TOST-1.0",
    url="https://openusd.org",
    project_urls={
        "Documentation": "https://openusd.org",
        "Developer Docs": "https://www.openusd.org/release/apiDocs.html",
        "Source": "https://github.com/PixarAnimationStudios/OpenUSD",
        "Discussion Group": "https://forum.openusd.org"
    },
    packages=setuptools.find_packages(os.path.join(BUILD_DIR, 'lib/python')),
    package_dir={"": os.path.join(BUILD_DIR, 'lib/python')},
    package_data={
        "": ["*.so", "*.dll", "*.pyd"],
        "pxr": ["pluginfo/*", "pluginfo/*/*", "pluginfo/*/*/*"],
    },
    classifiers=[
        "Programming Language :: Python :: 3",
        "Operating System :: POSIX :: Linux",
        "Operating System :: MacOS :: MacOS X",
        "Operating System :: Microsoft :: Windows :: Windows 10",
        "Environment :: Console",
        "Topic :: Multimedia :: Graphics",
    ],
    python_requires='>=3.8, <3.14',
)
