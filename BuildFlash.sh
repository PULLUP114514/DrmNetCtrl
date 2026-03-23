cd ~/myFiles/Proj/ePass/App/ipcController
make
scp -O -r ./dist/IpcController/ root@192.168.137.2:/app/
