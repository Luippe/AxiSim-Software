#!/usr/bin/env bash
set -euo pipefail

version="${1:-}"
build_dir="${BUILD_DIR:-out/build/linux-release}"
package_name="AxiSim-linux-x86_64"

if [[ -n "$version" ]]; then
    if [[ ! "$version" =~ ^[A-Za-z0-9._-]+$ ]]; then
        echo "Version may contain only letters, numbers, dots, underscores, and hyphens" >&2
        exit 1
    fi
    package_name="AxiSim-${version}-linux-x86_64"
fi

if [[ ! -d "$build_dir" ]]; then
    echo "Build directory not found: $build_dir" >&2
    echo "Configure it first with: cmake --preset linux-release" >&2
    exit 1
fi

cmake --build "$build_dir" --target AxiSim

stage_dir="dist/$package_name"
archive="dist/$package_name.tar.gz"

rm -rf -- "$stage_dir"
mkdir -p -- "$stage_dir"
cmake --install "$build_dir" --prefix "$stage_dir"

required=(
    "AxiSim"
    "axisim"
    "assets/fonts/Roboto-Regular.ttf"
    "graphics/shaders/mesh.vert"
)

for path in "${required[@]}"; do
    if [[ ! -e "$stage_dir/$path" ]]; then
        echo "Package is missing required file: $path" >&2
        exit 1
    fi
done

if ! compgen -G "$stage_dir/libgmsh.so*" >/dev/null; then
    echo "Package is missing libgmsh.so" >&2
    exit 1
fi

if ! compgen -G "$stage_dir/libcudart.so*" >/dev/null; then
    echo "Package is missing the CUDA runtime (libcudart.so)" >&2
    exit 1
fi

rm -f -- "$archive"
tar -C dist -czf "$archive" "$package_name"

echo "Linux package ready: $archive"
echo "Users can extract it and run ./$package_name/axisim"
