cd build
mkdir nvui
cd nvui
mkdir bin
cd ..
(gci -Path ./* -Include nvui.exe, *.dll, *.conf).fullname | foreach {Copy-Item -Force -Path $_ -Destination nvui/bin}
Copy-Item -Force -Recurse -Path plugins -Destination nvui/bin
Copy-Item -Force -Recurse -Path ../vim -Destination nvui
Compress-Archive -Force -Path nvui -DestinationPath nvui.zip
Move-Item -Force -Path nvui.zip -Destination ../
cd ..
