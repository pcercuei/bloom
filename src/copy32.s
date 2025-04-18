! SPDX-License-Identifier: GPL-2.0-only
!
!  32-byte aligned copy
!
!  Copyright (C) 2025 Paul Cercueil <paul@crapouillou.net>

.text
.globl _copy32
.type _copy32,%function

.align 5

! void copy32(void *dst, const void *src);
! dest/src must be 32-byte aligned.
_copy32:
	movca.l	r0,@r4
	fschg

	fmov	@r5+,dr0
	fmov	@r5+,dr2
	fmov	@r5+,dr4
	fmov	@r5+,dr6

	fmov.d	dr0,@r4
	add	#8,r4
	fmov.d	dr2,@r4
	add	#8,r4
	fmov.d	dr4,@r4
	add	#8,r4
	fmov.d	dr6,@r4

	rts
	fschg
