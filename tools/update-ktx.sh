#!/usr/bin/env bash
# Replaces external/ktx with the part of a KTX-Software release the engine builds.
#
#   tools/update-ktx.sh <tag>      e.g. tools/update-ktx.sh v4.4.2
#
# The engine uses libktx's KTX2 reader and writer: the files below, under 1 MB of the 550 MB repository (its tests,
# tools, Basis Universal, the ASTC encoder and its copy of zstd are left out). They keep their upstream paths;
# external/CMakeLists.txt builds them as the `ktx` target, against the zstd submodule (external/zstd) and with
# external/ktx_stubs.c for the zlib and KTX1 code libktx refers to but the engine never runs. Only these files are
# downloaded (a sparse, blob-filtered clone).
#
# Commit the result on its own ("Update KTX-Software to <tag>"); its hash goes into .git-blame-ignore-revs. A file
# a new release needs that is not listed shows up as a compile or link error: add it below (sources also to the
# `ktx` target).

set -euo pipefail

if [[ $# -ne 1 ]]; then
    sed -n '2,4p' "$0" | sed 's/^# \{0,1\}//'
    exit 2
fi
tag=$1

files=(
    # Licenses
    LICENSE.md
    LICENSES/
    external/dfdutils/LICENSE.adoc
    # Public headers
    include/ktx.h
    include/KHR/khr_df.h
    other_include/KHR/khrplatform.h
    # libktx: KTX2 reading and writing
    lib/basis_sgd.h
    lib/checkheader.c
    lib/filestream.c
    lib/filestream.h
    lib/formatsize.h
    lib/gl_format.h
    lib/hashlist.c
    lib/ktxint.h
    lib/memstream.c
    lib/memstream.h
    lib/strings.c
    lib/swap.c
    lib/texture.c
    lib/texture.h
    lib/texture1.h
    lib/texture2.c
    lib/texture2.h
    lib/texture_funcs.inl
    lib/uthash.h
    lib/vk_format.h
    lib/vkformat_check.c
    lib/vkformat_check_variant.c
    lib/vkformat_enum.h
    lib/vkformat_typesize.c
    lib/writer2.c
    utils/unused.h
    # Data format descriptors
    external/dfdutils/colourspaces.c
    external/dfdutils/createdfd.c
    external/dfdutils/dfd.h
    external/dfdutils/interpretdfd.c
    external/dfdutils/queries.c
    external/dfdutils/vk2dfd.c
    external/dfdutils/vk2dfd.inl
)

top=$(git rev-parse --show-toplevel)
dest=$top/external/ktx
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

src=$tmp/KTX-Software
git -c core.autocrlf=false clone --quiet --depth 1 --branch "$tag" --filter=blob:none --no-checkout \
    https://github.com/KhronosGroup/KTX-Software.git "$src"
printf '/%s\n' "${files[@]}" | git -C "$src" sparse-checkout set --no-cone --stdin
git -C "$src" -c core.autocrlf=false -c core.eol=lf -c advice.detachedHead=false checkout --quiet "$tag"
commit=$(git -C "$src" rev-parse HEAD)

for file in "${files[@]}"; do
    if [[ ! -e "$src/$file" ]]; then
        echo "KTX-Software $tag has no '$file'" >&2
        exit 1
    fi
done

rm -rf "$dest"
for file in "${files[@]}"; do
    mkdir -p "$dest/$(dirname "$file")"
    # -L: upstream has symlinks; copy what they point to, which works without symlink support on Windows.
    cp -RL "$src/$file" "$dest/$(dirname "$file")/"
done
printf '%s\n%s\n' "$tag" "$commit" >"$dest/IMPORTED_VERSION"

echo "external/ktx is KTX-Software $tag ($commit), $(du -sh "$dest" | cut -f1)"
