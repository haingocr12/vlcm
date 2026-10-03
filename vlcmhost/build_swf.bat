@echo off
rem Build VlcmLoader.swf tu VlcmLoader.as (cung cach da build ban gui kem).
rem Can: Java 11+ va Node.js. Chuan bi mot lan trong thu muc nay:
rem   npm i --ignore-scripts @apache-royale/royale-js-swf
rem   git clone --depth 1 https://github.com/nexussays/playerglobal
setlocal
set ROOT=%~dp0
set MXMLC=%ROOT%node_modules\@apache-royale\royale-js-swf\royale-asjs\lib\mxmlc.jar
set PG=%ROOT%playerglobal\11.8\playerglobal.swc
if not exist "%MXMLC%" (echo Thieu %MXMLC% & exit /b 1)
if not exist "%PG%" (echo Thieu %PG% & exit /b 1)
> "%ROOT%empty-config.xml" echo ^<royale-config/^>
java -jar "%MXMLC%" -load-config="%ROOT%empty-config.xml" -external-library-path+="%PG%" ^
  -target-player=11.8 -swf-version=21 -use-network=true -optimize=true -debug=false ^
  -compiler.compress=false -output="%ROOT%VlcmLoader.swf" "%ROOT%VlcmLoader.as"
