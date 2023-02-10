#! /bin/bash
if [ -d ./contrib/rapidjson/thirdparty ]
then
    rm -rf ./contrib/rapidjson/thirdparty
fi
cmake --preset armv7
pushd armv7/build
cmake --build .
cmake --install .
popd