#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 4 ]; then
  echo "usage: $0 <display-version> <vst3-source> <au-source> <output-dir>" >&2
  exit 1
fi

display_version="$1"
vst3_source="$2"
au_source="$3"
output_dir="$4"

script_dir="$(cd "$(dirname "$0")" && pwd)"
work_dir="${output_dir}/macos-installer-work"
pkg_dir="${work_dir}/packages"
resources_dir="${work_dir}/resources"
payload_au="${work_dir}/payload-au"
payload_vst3="${work_dir}/payload-vst3"
pkg_version="${display_version#v}"
pkg_version="${pkg_version%%-*}"

rm -rf "$work_dir"
mkdir -p "$pkg_dir" "$resources_dir"
mkdir -p "${payload_au}/Library/Audio/Plug-Ins/Components"
mkdir -p "${payload_vst3}/Library/Audio/Plug-Ins/VST3"

cp -R "$au_source" "${payload_au}/Library/Audio/Plug-Ins/Components/topaz pan.component"
cp -R "$vst3_source" "${payload_vst3}/Library/Audio/Plug-Ins/VST3/topaz pan.vst3"

pkgbuild \
  --root "$payload_au" \
  --identifier "com.topaz.topaz-pan.au" \
  --version "$pkg_version" \
  --install-location "/" \
  "${pkg_dir}/topaz-pan-au.pkg"

pkgbuild \
  --root "$payload_vst3" \
  --identifier "com.topaz.topaz-pan.vst3" \
  --version "$pkg_version" \
  --install-location "/" \
  "${pkg_dir}/topaz-pan-vst3.pkg"

sed \
  -e "s|@DISPLAY_VERSION@|${display_version}|g" \
  -e "s|@PKG_VERSION@|${pkg_version}|g" \
  "${script_dir}/Distribution.xml.in" > "${work_dir}/Distribution.xml"

sed \
  -e "s|@DISPLAY_VERSION@|${display_version}|g" \
  "${script_dir}/welcome.html.in" > "${resources_dir}/welcome.html"

productbuild \
  --distribution "${work_dir}/Distribution.xml" \
  --resources "${resources_dir}" \
  --package-path "${pkg_dir}" \
  "${output_dir}/topaz-pan-${display_version}-macos-installer.pkg"
