#include "mbcs.h"

bool mb_is_lead(MbKind kind, int b) {
	switch (kind) {
	case MB_GBK:   return b >= 0x81 && b <= 0xFE;
	case MB_BIG5:  return b >= 0xA1 && b <= 0xF9;
	case MB_SJIS:  return (b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC);
	case MB_EUCKR: return b >= 0x81 && b <= 0xFE;
	default:       return false;
	}
}

uint16_t mb_decode(MbKind kind, int lead, int trail) {
	if (!mb_is_lead(kind, lead)) return 0;
	switch (kind) {
	case MB_GBK:
		if (trail < 0x40 || trail > 0xFE) return 0;
		return mb_gbk[(lead - 0x81) * 191 + (trail - 0x40)];
	case MB_BIG5:
		if (trail < 0x40 || trail > 0xFE) return 0;
		return mb_big5[(lead - 0xA1) * 191 + (trail - 0x40)];
	case MB_SJIS: {
		if (trail < 0x40 || trail > 0xFC) return 0;
		int row = lead <= 0x9F ? lead - 0x81 : lead - 0xE0 + 31;
		return mb_sjis[row * 189 + (trail - 0x40)];
	}
	case MB_EUCKR:
		if (trail < 0x41 || trail > 0xFE) return 0;
		return mb_euckr[(lead - 0x81) * 190 + (trail - 0x41)];
	default:
		return 0;
	}
}
