param(
 [string]$SDK='D:/program files/Huawei/DevEco Studio/sdk/default/openharmony/native',
 [string]$BuildRoot='F:/HarmonyPC/termharbor-runtime-build',
 [string]$InstallRoot='D:/HarmonyPC-tools/termharbor-runtime/arm64-v8a',
 [string]$EvidenceRoot='E:/Codex/termharbor-runtime-build'
)
$ErrorActionPreference='Stop'
$archiveRoot=Join-Path $PSScriptRoot '../third_party/runtime-sources'
$manifest=Get-Content -LiteralPath (Join-Path $archiveRoot 'sources.json') -Raw|ConvertFrom-Json
function Admit-RuntimeWork {
 $cpu=(Get-Counter '\Processor(_Total)\% Processor Time' -SampleInterval 1 -MaxSamples 3).CounterSamples.CookedValue
 if(($cpu|Measure-Object -Maximum).Maximum -ge 70){throw 'CPU admission denied; wait and rerun'}
}
Admit-RuntimeWork
$own=[Diagnostics.Process]::GetCurrentProcess();$own.ProcessorAffinity=3;$own.PriorityClass='BelowNormal'
New-Item -ItemType Directory -Force -Path $BuildRoot,$EvidenceRoot | Out-Null
foreach($source in $manifest){
 $archive=Join-Path $archiveRoot $source.file
 if((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLower() -ne $source.sha256){throw 'Pinned source hash mismatch'}
 $directory=($source.file -replace '\.tar\.(bz2|xz|gz)$','')
 if(!(Test-Path -LiteralPath (Join-Path $BuildRoot $directory))){ & tar -xf $archive -C $BuildRoot; if($LASTEXITCODE -ne 0){throw 'Source extraction failed'} }
}

# Build mbedtls from pinned complete sources
& {
$ErrorActionPreference='Stop'
$root=$BuildRoot
$sdk=$SDK
$cmake=$sdk+'\build-tools\cmake\bin\cmake.exe'
$prefix=$InstallRoot
$log=Join-Path $EvidenceRoot 'mbedtls-build.log'
function Admit-Work {
 $samples=(Get-Counter '\Processor(_Total)\% Processor Time' -SampleInterval 1 -MaxSamples 3).CounterSamples.CookedValue
 if(($samples|Measure-Object -Maximum).Maximum -ge 70){throw 'RESOURCE_WAIT'}
}
Admit-Work
$own=[Diagnostics.Process]::GetCurrentProcess();$own.ProcessorAffinity=3;$own.PriorityClass='BelowNormal'
$config=$root+'\mbedtls-3.6.7\include\mbedtls\mbedtls_config.h'
$text=[IO.File]::ReadAllText($config)
if(-not $text.Contains('TermHarbor local review threaded crypto')){
 $text="#define MBEDTLS_THREADING_C`n#define MBEDTLS_THREADING_PTHREAD`n/* TermHarbor local review threaded crypto */`n"+$text
 [IO.File]::WriteAllText($config,$text,[Text.UTF8Encoding]::new($false))
}
& $cmake -S ($root+'\mbedtls-3.6.7') -B ($root+'\mbedtls-threaded-build') -G Ninja "-DCMAKE_MAKE_PROGRAM=$sdk/build-tools/cmake/bin/ninja.exe" "-DCMAKE_TOOLCHAIN_FILE=$sdk/build/cmake/ohos.toolchain.cmake" -DOHOS_ARCH=arm64-v8a -DOHOS_PLATFORM=OHOS -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS=-Wno-unused-command-line-argument -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DENABLE_TESTING=OFF -DENABLE_PROGRAMS=OFF -DUSE_STATIC_MBEDTLS_LIBRARY=ON -DUSE_SHARED_MBEDTLS_LIBRARY=OFF "-DCMAKE_INSTALL_PREFIX=$prefix" *> $log
if($LASTEXITCODE -ne 0){throw 'Threaded crypto configure failed'}
Admit-Work
& $cmake --build ($root+'\mbedtls-threaded-build') --parallel 2 *>> $log
if($LASTEXITCODE -ne 0){throw 'Threaded crypto build failed'}
& $cmake --install ($root+'\mbedtls-threaded-build') *>> $log
if($LASTEXITCODE -ne 0){throw 'Threaded crypto install failed'}
Write-Output 'Isolated threaded Mbed TLS installed'
}


# Build libssh from pinned complete sources
& {
$ErrorActionPreference='Stop'
$root=$BuildRoot
$sdk=$SDK
$cmake=$sdk+'\build-tools\cmake\bin\cmake.exe'
$crypto=$InstallRoot
$build=$root+'\libssh-build'
$log=Join-Path $EvidenceRoot 'libssh-build.log'
function Admit-Work {
  $samples=(Get-Counter '\Processor(_Total)\% Processor Time' -SampleInterval 1 -MaxSamples 3).CounterSamples.CookedValue
  if(($samples|Measure-Object -Maximum).Maximum -ge 70){throw 'RESOURCE_WAIT'}
}
Admit-Work
$own=[Diagnostics.Process]::GetCurrentProcess();$own.ProcessorAffinity=3;$own.PriorityClass='BelowNormal'
$env:CMAKE_BUILD_PARALLEL_LEVEL='2'
& $cmake -S ($root+'\libssh-0.12.2') -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$sdk/build-tools/cmake/bin/ninja.exe" "-DCMAKE_TOOLCHAIN_FILE=$sdk/build/cmake/ohos.toolchain.cmake" -DOHOS_ARCH=arm64-v8a -DOHOS_PLATFORM=OHOS -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=OFF -DWITH_MBEDTLS=ON -DWITH_GCRYPT=OFF -DWITH_GSSAPI=OFF -DWITH_ZLIB=OFF -DWITH_EXAMPLES=OFF -DWITH_SERVER=ON -DWITH_SFTP=ON -DWITH_EXEC=OFF -DWITH_SYMBOL_VERSIONING=OFF -DWITH_NACL=OFF -DWITH_PKCS11_URI=OFF -DWITH_PKCS11_PROVIDER=OFF "-DMBEDTLS_INCLUDE_DIR=$crypto/include" "-DMBEDTLS_CRYPTO_LIBRARY=$crypto/lib/libmbedcrypto.a" "-DMBEDTLS_SSL_LIBRARY=$crypto/lib/libmbedtls.a" "-DMBEDTLS_X509_LIBRARY=$crypto/lib/libmbedx509.a" "-DCMAKE_INSTALL_PREFIX=$InstallRoot" *> $log
if($LASTEXITCODE -ne 0){throw 'libssh configure failed; see E log'}
Admit-Work
& $cmake --build $build --parallel 2 *>> $log
if($LASTEXITCODE -ne 0){throw 'libssh build failed; see E log'}
& $cmake --install $build *>> $log
if($LASTEXITCODE -ne 0){throw 'libssh install failed; see E log'}
Write-Output 'libssh configure/build/install completed'
}


# Build libssh2 from pinned complete sources
& {
$ErrorActionPreference='Stop'
$root=$BuildRoot
$sdk=$SDK
$cmake=$sdk+'\build-tools\cmake\bin\cmake.exe'
$prefix=$InstallRoot
$log=Join-Path $EvidenceRoot 'libssh2-build.log'
function Admit { $c=(Get-Counter '\Processor(_Total)\% Processor Time' -SampleInterval 1 -MaxSamples 3).CounterSamples.CookedValue; if(($c|Measure-Object -Maximum).Maximum -ge 70){throw 'RESOURCE_WAIT'} }
Admit
$p=[Diagnostics.Process]::GetCurrentProcess();$p.ProcessorAffinity=3;$p.PriorityClass='BelowNormal'
& $cmake -S ($root+'\libssh2-1.11.1') -B ($root+'\libssh2-threaded-build') -G Ninja "-DCMAKE_MAKE_PROGRAM=$sdk/build-tools/cmake/bin/ninja.exe" "-DCMAKE_TOOLCHAIN_FILE=$sdk/build/cmake/ohos.toolchain.cmake" -DOHOS_ARCH=arm64-v8a -DOHOS_PLATFORM=OHOS -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DBUILD_SHARED_LIBS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DCRYPTO_BACKEND=mbedTLS -DENABLE_ZLIB_COMPRESSION=OFF "-DMBEDTLS_INCLUDE_DIR=$prefix/include" "-DMBEDCRYPTO_LIBRARY=$prefix/lib/libmbedcrypto.a" "-DCMAKE_INSTALL_PREFIX=$prefix" *> $log
if($LASTEXITCODE -ne 0){throw 'libssh2 configure failed'}
Admit
& $cmake --build ($root+'\libssh2-threaded-build') --parallel 2 *>> $log
if($LASTEXITCODE -ne 0){throw 'libssh2 build failed'}
& $cmake --install ($root+'\libssh2-threaded-build') *>> $log
if($LASTEXITCODE -ne 0){throw 'libssh2 install failed'}
Write-Output 'Consistent libssh2 installed'
}
