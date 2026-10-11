#!/usr/bin/env bash
# Test helpers for driving the emulator from a shell:  source emu/tap.sh
E=${EMU_URL:-http://localhost:8090}
p() { curl -s -X POST -d '{}' "$E$1" > /dev/null; }
tap() { p "/touch?x=$1&y=$2&down=1"; sleep 0.2; p "/touch?x=$1&y=$2&down=0"; sleep "${3:-0.4}"; }
setv() { p "/set?device=$1&key=$2&value=$3"; }
fw() { curl -s "$E/state?log=999999999" | /c/tools/Brew_by_Weight/bdb-bbw/.venv/Scripts/python.exe -c "import sys,json;s=json.load(sys.stdin);print(s['firmware'],'| liquid %.1f pump %s bot %s'%(s['machine']['liquidG'],s['machine']['brewing'],s['bot']['linked']))"; }
logs() { curl -s "$E/state?log=${1:-0}" | /c/tools/Brew_by_Weight/bdb-bbw/.venv/Scripts/python.exe -c "import sys,json;[print('%8.3f %-7s %s'%(l[0]/1000,l[1],l[2])) for l in json.load(sys.stdin)['log']]"; }
