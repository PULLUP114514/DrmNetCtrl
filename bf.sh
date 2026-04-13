cd ~/myFiles/Proj/ePass/espHostController/UartIpcController/build/
make -j16
cp ./UartController ../dist/UartController/
cp ../src/appconfig.json ../dist/UartController/
scp -O -r ../dist/UartController/ root@192.168.137.2:/app/
