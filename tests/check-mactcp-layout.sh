#!/bin/bash
# Compares app/mactcp.h's layouts with Apple's MacTCP.h (talkip carries a
# copy: TALKIP_DIR, default ../talkip), offset by offset, with Retro68's
# compiler. Prints the differences; exits non-zero when there are any.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
RETRO68=${RETRO68:-$HOME/emulators/tools/retro68-build/toolchain}
TALKIP_DIR=${TALKIP_DIR:-$here/../talkip}
cc="$RETRO68/bin/m68k-apple-macos-gcc"
apple="$TALKIP_DIR/include/MacTCP.h"
[ -f "$apple" ] || { echo "check-mactcp-layout: no Apple MacTCP.h at $apple" >&2; exit 2; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
fields='
TCPiopb ioCompletion
TCPiopb ioResult
TCPiopb ioCRefNum
TCPiopb csCode
TCPiopb tcpStream
TCPiopb csParam
TCPiopb csParam.create.rcvBuff
TCPiopb csParam.create.rcvBuffLen
TCPiopb csParam.create.notifyProc
TCPiopb csParam.create.userDataPtr
TCPiopb csParam.open.ulpTimeoutValue
TCPiopb csParam.open.ulpTimeoutAction
TCPiopb csParam.open.validityFlags
TCPiopb csParam.open.commandTimeoutValue
TCPiopb csParam.open.remoteHost
TCPiopb csParam.open.remotePort
TCPiopb csParam.open.localHost
TCPiopb csParam.open.localPort
TCPiopb csParam.open.optionCnt
TCPiopb csParam.open.userDataPtr
TCPiopb csParam.send.ulpTimeoutValue
TCPiopb csParam.send.validityFlags
TCPiopb csParam.send.pushFlag
TCPiopb csParam.send.wdsPtr
TCPiopb csParam.send.sendFree
TCPiopb csParam.send.sendLength
TCPiopb csParam.send.userDataPtr
TCPiopb csParam.receive.commandTimeoutValue
TCPiopb csParam.receive.rcvBuff
TCPiopb csParam.receive.rcvBuffLen
TCPiopb csParam.receive.rdsPtr
TCPiopb csParam.receive.userDataPtr
TCPiopb csParam.close.ulpTimeoutValue
TCPiopb csParam.close.validityFlags
TCPiopb csParam.close.userDataPtr
wdsEntry length
wdsEntry ptr
'
for which in topica apple; do
    {
        if [ $which = topica ]; then echo "#include \"$here/app/mactcp.h\""; else echo "#include \"$apple\""; fi
        echo '#include <stddef.h>'
        echo 'unsigned short layout[] = {'
        echo "$fields" | while read -r type field; do if [ -n "$type" ]; then echo "    offsetof($type, $field),"; fi; done
        echo '    sizeof(wdsEntry) };'
    } > "$tmp/$which.c"
    "$cc" -m68000 -S -o - "$tmp/$which.c" | awk '$1 == ".short" || $1 == ".word" { print $2 }' > "$tmp/$which.txt"
done
paste <(echo "$fields" | sed '/^$/d'; echo "sizeof wdsEntry") "$tmp/topica.txt" "$tmp/apple.txt" |
    awk -F'\t' '$2 != $3 { print "differs: " $1 ": topica" $2 ", apple" $3; bad = 1 } END { exit bad }' &&
    echo "check-mactcp-layout: $(echo "$fields" | sed '/^$/d' | wc -l) offsets match Apple's"
