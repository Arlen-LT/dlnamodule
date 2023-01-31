param ($cmake_install)
echo "CMake Install Path: $cmake_install"

$vs2022_version=$(& "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -version [17.0,18.0]  -property catalog_productDisplayVersion)
$vs2022_install=$(& "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -version [17.0,18.0] -property installationPath)
echo "Visual Studio Version: $vs2022_version"
echo "Visual Studio Install Path: $vs2022_install"

$cmake="$cmake_install/bin/cmake.exe"
& $cmake --version
# $ninja="C:\tools\cmake-3.24.3-windows-x86_64\bin\ninja.exe"
# & $ninja --version
if((Test-Path -Path './contrib/rapidjson/thirdparty')){
	rm -r -force ./contrib/rapidjson/thirdparty
}
& $cmake --preset win64 -DBUILD_TESTING=FALSE -DUPNP_ENABLE_IPV6=FALSE -DUPNP_BUILD_SAMPLES=FALSE -DRAPIDJSON_BUILD_CXX11=FALSE -DRAPIDJSON_BUILD_CXX17=TRUE -DRAPIDJSON_BUILD_DOC=FALSE -DRAPIDJSON_BUILD_EXAMPLES=FALSE -DRAPIDJSON_BUILD_TESTS=FALSE
pushd win64/build
& $cmake --build . --config Release
& $cmake --install .
popd
