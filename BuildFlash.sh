cd ~/myFiles/Proj/ePass/App/ipcController
make
cp ./src/appconfig.json ./dist/IpcController/
scp -O -r ./dist/IpcController/ root@192.168.137.2:/app/
