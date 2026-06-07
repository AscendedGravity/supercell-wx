#!/bin/bash
script_source="${BASH_SOURCE[0]:-$0}"
script_dir="$(cd "$(dirname "${script_source}")" && pwd)"

# Assign user-specified Python Virtual Environment
if [ "${1:-}" = "none" ]; then
    unset venv_path
else
    venv_arg="${1:-${script_dir}/../.venv}"
    # Portable way to get absolute path without requiring the directory to exist
    case "${venv_arg}" in
        /*) venv_path="${venv_arg}" ;;
        *) venv_path="$(cd "$(dirname "${venv_arg}")" && pwd)/$(basename "${venv_arg}")" ;;
    esac
    export venv_path
fi

# Load custom build settings
if [ -f "${script_dir}/lib/user-setup.sh" ]; then
    source "${script_dir}/lib/user-setup.sh"
fi

# Activate Python Virtual Environment
if [ -n "${venv_path:-}" ]; then
    uv venv --clear "${venv_path}"
    source "${venv_path}/bin/activate"
fi

# Install Python packages
uv sync

# Configure default Conan profile
conan profile detect -e

# Conan profiles
if [[ "$(uname)" == "Darwin" ]]; then
    # macOS profiles
    conan_profiles=(
        "scwx-macos_clang-18"
        "scwx-macos_clang-18_armv8"
        "scwx-macos_clang-19_armv8"
        "scwx-macos_clang-20_armv8"
        "scwx-macos_clang-21_armv8"
        "scwx-macos_clang-22_armv8"
    )
else
    # Linux profiles
    conan_profiles=(
        "scwx-linux_clang-17"
        "scwx-linux_clang-17_armv8"
        "scwx-linux_clang-18"
        "scwx-linux_clang-18_armv8"
        "scwx-linux_clang-19"
        "scwx-linux_clang-19_armv8"
        "scwx-linux_clang-20"
        "scwx-linux_clang-20_armv8"
        "scwx-linux_clang-21"
        "scwx-linux_clang-21_armv8"
        "scwx-linux_clang-22"
        "scwx-linux_clang-22_armv8"
        "scwx-linux_gcc-11"
        "scwx-linux_gcc-11_armv8"
        "scwx-linux_gcc-12"
        "scwx-linux_gcc-12_armv8"
        "scwx-linux_gcc-13"
        "scwx-linux_gcc-13_armv8"
        "scwx-linux_gcc-14"
        "scwx-linux_gcc-14_armv8"
        "scwx-linux_gcc-15"
        "scwx-linux_gcc-15_armv8"
        "scwx-linux_gcc-16"
        "scwx-linux_gcc-16_armv8"
    )
fi

# Install Conan profiles
for profile_name in "${conan_profiles[@]}"; do
    # Install original profile
    conan config install "${script_dir}/conan/profiles/${profile_name}" -tf profiles

    # Create debug profile in temp directory
    debug_profile="/tmp/${profile_name}-debug"
    cp "${script_dir}/conan/profiles/${profile_name}" "${debug_profile}"

    # Replace build_type=Release with build_type=Debug
    if [[ "$(uname)" == "Darwin" ]]; then
        sed -i '' 's/build_type=Release/build_type=Debug/g' "${debug_profile}"
    else
        sed -i 's/build_type=Release/build_type=Debug/g' "${debug_profile}"
    fi

    # Install the debug profile
    conan config install "${debug_profile}" -tf profiles

    # Remove temporary debug profile
    rm "${debug_profile}"
done

# Deactivate Python Virtual Environment
if [ -n "${venv_path:-}" ]; then
    deactivate
fi
