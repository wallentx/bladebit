#!/usr/bin/env bash
set -eo pipefail
if [[ $RUNNER_DEBUG = 1 ]]; then
  set -x
fi

host_os=$(uname -a)
case "${host_os}" in
Linux*) host_os="linux" ;;
Darwin*) host_os="macos" ;;
CYGWIN*) host_os="windows" ;;
MINGW*) host_os="windows" ;;
*Msys) host_os="windows" ;;
esac

if [[ "$host_os" == "windows" ]]; then
  ext="zip"
else
  ext="tar.gz"
fi

if [[ "$host_os" == "macos" ]]; then
  procs=$(sysctl -n hw.logicalcpu)
  sha_sum="shasum -a 256"
else
  procs=$(nproc --all)
  sha_sum="sha256sum"
fi

artifact_name=green_reaper.$ext

while true; do
  case $1 in
  --artifact)
    shift && artifact_name=$1 || exit 1
    ;;
  esac
  shift || break
done

env | sort

echo "—————"

echo "${GITHUB_ENV}"

echo "—————"

echo "${GITHUB_OUTPUT}"

if [[ "$CI" == "true" ]]; then
  echo "CI true"
  #if [[ "$host_os" == "windows" ]] || [[ "$host_os" == "linux" ]]; then
#    cat "${artifact_name}.sha256.txt" | while IFS= read -r line; do
      #echo -e "$(echo ${line#* } | tr -d '*')\n###### <sup>${line%%*}</sup>\n"
#    done >summary.md

#    echo "$OBJDUMP" >>summary.md
  #fi
echo "hi" >> "$GITHUB_STEP_SUMMARY"
#  if [[ "$host_os" == "windows" ]]; then
#    harvester_artifact_path="$(cygpath -m "$(pwd)/${artifact_name}")*"
#  else
#    harvester_artifact_path="$(pwd)/${artifact_name}*"
#  fi
#  echo "harvester_artifact_path=$harvester_artifact_path"
#  echo "harvester_artifact_path=$harvester_artifact_path" >>"$GITHUB_ENV"
fi
