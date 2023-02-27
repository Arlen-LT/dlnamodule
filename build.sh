#!/bin/bash -xe

usage(){
	echo "Usage: build.sh [options]"
	echo "--arch[=]<arch>  Build for specified architecture (supported: armv7l, arm64, win64)"
	exit 0
}

# $@ is all command line parameters passed to the script.
# -o is for short options like -v
# -l is for long options with double dash like --version
# the comma separates different long options
# -a is for long options with single dash like -version
options=$(getopt -n "Building SMBModule" -l "help,version,arch:" -o "hv" -a -- "$@")
eval set -- ${options}
while true
do
    case "$1" in
    --arch) shift; arch=$1 ;;
    -h | --help) usage ;;
    --) shift; break;;
	esac
    shift
done

if [ ! -d "contrib/slog" ]; then
    if [ "${arch}" == "armv7" ]; then
        job_name=build_${arch}l
    elif [ "${arch}" == "arm64" ]; then
        job_name=build_${arch}
    fi

    pushd contrib
    curl --location \
        --header "PRIVATE-TOKEN: ${PRIVATE_TOKEN}" \
        "http://gitlab.sourcetech.local/api/v4/projects/44/jobs/artifacts/main/download?job=${job_name}" \
        --output slog.zip && 7z x slog.zip && rm slog.zip && mv out slog
    popd
fi

cmake --version
cmake --preset ${arch}
pushd ${arch}/build
cmake --build .
cmake --install .
popd