@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
cl /nologo /std:c++17 /EHsc /O2 /MD /I . /I MBA /I include /I c:\libs\include MBA\Node.cpp MBA\Parser.cpp MBA\Batch.cpp MBA\RefineA.cpp MBA\RefineB.cpp MBA\RefineC.cpp MBA\RefineD.cpp MBA\Expand.cpp MBA\Substitute.cpp MBA\Bitwise.cpp MBA\Implicant.cpp MBA\Dnf.cpp MBA\BitwiseFactory.cpp MBA\LinearSimplifier.cpp MBA\GeneralSimplifier.cpp MBA\Verify.cpp MBA\mba_cli.cpp /Fe:MBA\build\mba_cli.exe /Fo:MBA\build\ c:\libs\lib\LLVMSupport.lib "C:\Program Files (x86)\Windows Kits\10\Lib\10.0.22621.0\um\x64\ntdll.lib"
exit /b %ERRORLEVEL%
