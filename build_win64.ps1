param ($cmake_install)
echo "CMake Install Path: $cmake_install"

$vs2022_version=$(& "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -version [17.0,18.0]  -property catalog_productDisplayVersion)
$vs2022_install=$(& "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -version [17.0,18.0] -property installationPath)
echo "Visual Studio Version: $vs2022_version"
echo "Visual Studio Install Path: $vs2022_install"

$cmake="$cmake_install/bin/cmake.exe"
$ctest="$cmake_install/bin/ctest.exe"

& $cmake --version
& $cmake --preset win64
pushd win64/build
& $cmake --build . --config Release
& $cmake --install .
& $ctest
popd
