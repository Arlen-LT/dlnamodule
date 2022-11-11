$vs2022_version=$(& "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -version [17.0,18.0]  -property catalog_productDisplayVersion)
$vs2022_install=$(& "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -version [17.0,18.0] -property installationPath)
echo "Visual Studio Version: $vs2022_version"
echo "Visual Studio Install Path: $vs2022_install"

$cmake="$WINRUNNER_CMAKE_DIR/bin/cmake.exe"
& $cmake --version
# $ninja="C:\tools\cmake-3.24.3-windows-x86_64\bin\ninja.exe"
# & $ninja --version
& $cmake --preset win64 -G"Visual Studio 17 2022"
pushd win64/build
& $cmake --build .
& $cmake --install .
popd
