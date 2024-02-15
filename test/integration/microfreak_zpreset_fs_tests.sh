#!/usr/bin/env bash

#Zip files include date information, hence generic testing is not possible.

echo "Backing up preset..."
$ecli microfreak-preset-dl $TEST_DEVICE:/512
[ $? -ne 0 ] && exit 1
FILE=$(echo "$srcdir/Arturia MicroFreak preset 512"*.syx)
BACKUP=$srcdir/backup.syx
mv "$FILE" $srcdir/backup.syx

$srcdir/integration/generic_fs_tests.sh --no-download microfreak zpreset / 512 "/0 /513" /512 "" ""
[ $? -ne 0 ] && exit 1

echo "Testing download..."
$ecli microfreak-zpreset-dl $TEST_DEVICE:/512
[ $? -ne 0 ] && exit 1
FILE=$(echo "$srcdir/Arturia MicroFreak zpreset 512"*.mfpz)
[ ! -f "$FILE" ] && exit 1
exp=$(cksum "$srcdir/res/connectors/microfreak_preset.data" | awk '{print $1}')
act=$(unzip -p "$FILE" "0_preset" | cksum | awk '{print $1}')
[ "$exp" != "$act" ] && exit 1
rm "$FILE"

echo "Restoring preset..."
$ecli microfreak-preset-ul $BACKUP $TEST_DEVICE:/512
rm "$BACKUP"

exit $?
