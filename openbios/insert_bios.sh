#!/bin/sh

BSS_ADDR=`$KOS_CC_PREFIX-readelf -s $2 |grep ' __bss_start' |awk '{print $2}'`

$KOS_CC_PREFIX-objcopy --add-section .bios=$1 $2
$KOS_CC_PREFIX-objcopy --change-section-address .bios=0x$BSS_ADDR $2
$KOS_CC_PREFIX-objcopy --set-section-flags .bios=alloc $2 2>/dev/null
