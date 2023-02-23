#!/bin/bash

pushd contrib
curl --location \
    --header "PRIVATE-TOKEN: ${PRIVATE_TOKEN}" \
    "http://gitlab.sourcetech.local/api/v4/projects/44/jobs/artifacts/main/download?job=build_armv7l" \
    --output slog.zip && 7z x slog.zip && mv out slog && rm slog.zip
popd

cmake --version
cmake --preset armv7
pushd armv7/build
cmake --build .
cmake --install .
popd