#!/bin/sh

dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

exists() {
  command -v "$1" > /dev/null
}

download_curl() {
  _name=$1
  shift
  _url=$1
  shift
  curl -L "$_url" | tar --extract "--one-top-level=$dir/build/$_name" --strip-components=1 --file - "$@"
  echo "Downloaded $_name from $_url"
  unset _name _url
}

download_wget() {
  _name=$1
  shift
  _url=$1
  shift
  wget -qO- "$_url" | tar --extract "--one-top-level=$dir/build/$_name" --strip-components=1 --file - "$@"
  echo "Downloaded $_name from $_url"
  unset _name _url
}

download() {
  if [ -e "$dir/build/$1" ]; then
    return 0
  fi
  
  if exists curl; then
    download_curl "$@"
    return
  fi

  if exists wget; then
    download_wget "$@"
    return
  fi

  return 1;
}

mkdir -p "$dir/build"

download "imgui" "https://github.com/ocornut/imgui/archive/refs/tags/v1.89.5.tar.gz" -z
download "glfw" "https://github.com/glfw/glfw/archive/refs/tags/3.3.8.tar.gz" -z
download "cpp-json" "https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz" -J
