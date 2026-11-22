cd "$(dirname "$0")/.."
bash Tools/uqueue.sh run grey
P="$(cygpath -w "$PWD")"
MSYS_NO_PATHCONV=1 timeout 900 "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P\Jeebsland.uproject" "-ExecutePythonScript=$P\Tools\\$1" -unattended -nosplash -RenderOffScreen -Multiprocess -LiveCoding=false "-abslog=$P\Saved\Logs\grey.log" > /dev/null 2>&1
echo RUNEND >> Saved/Logs/grey.log
bash Tools/uqueue.sh release grey
