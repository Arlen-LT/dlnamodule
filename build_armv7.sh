#! /bin/bash
if [ -d ./contrib/rapidjson/thirdparty ]
then
    rm -rf ./contrib/rapidjson/thirdparty
fi
cmake --preset armv7 -DENABLE_SLOG=TRUE -DBUILD_TESTING=FALSE -DUPNP_ENABLE_IPV6=FALSE -DRAPIDJSON_BUILD_CXX11=FALSE -DRAPIDJSON_BUILD_CXX17=TRUE -DRAPIDJSON_BUILD_DOC=FALSE -DRAPIDJSON_BUILD_EXAMPLES=FALSE -DRAPIDJSON_BUILD_TESTS=FALSE
pushd armv7/build
cmake --build .
cmake --install .
popd