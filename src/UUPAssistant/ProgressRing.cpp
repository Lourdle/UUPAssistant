#include "pch.h"
#include "UUPAssistant.h"

using namespace Lourdle::UIFramework;

namespace
{
	constexpr const char* kProgressRingFontName = "Segoe Boot Semilight";

	constexpr UINT kLegacyFrameIntervalMs = 24;
	constexpr UINT kModernFrameIntervalMs = 16;

	constexpr WCHAR kLegacyGlyphFirst = 0xE052;
	constexpr WCHAR kLegacyGlyphLast = 0xE0CB;
	constexpr WCHAR kModernGlyphFirst = 0xE100;
	constexpr WCHAR kModernGlyphLast = 0xE175;

	constexpr WORD kMissingGlyphIndex = 0xFFFF;
}


template<bool Legacy>
static VOID CALLBACK PRTimerProc(HWND hwnd, UINT uMsg, ProgressRing* p, DWORD dwTime)
{
	if (p->Code == (Legacy ? kLegacyGlyphLast : kModernGlyphLast))
		p->Code = Legacy ? kLegacyGlyphFirst : kModernGlyphFirst;
	else
		++p->Code;

	HDC hdc = GetDC(hwnd);
	p->OnPaint(hdc);
	ReleaseDC(hwnd, hdc);
}

inline
static bool CheckNewStyleAvailable()
{
	HFONT hFont = CreateFontA(30, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_QUALITY, kProgressRingFontName);
	HDC hdc = GetDC(nullptr);
	hFont = SelectObject(hdc, hFont);

	WCHAR wch = kModernGlyphFirst;
	WORD gi = kMissingGlyphIndex;
	GetGlyphIndicesW(hdc, &wch, 1, &gi, GGI_MARK_NONEXISTING_GLYPHS);

	hFont = SelectObject(hdc, hFont);
	DeleteObject(hFont);
	ReleaseDC(nullptr, hdc);
	return gi != kMissingGlyphIndex;
}


ProgressRing::ProgressRing(WindowBase* Parent, bool Legacy) : Window(0, 0, 0, 0, nullptr, WS_CHILD | WS_VISIBLE, Parent->GetHandle()),
Legacy(Legacy ? Legacy : !CheckNewStyleAvailable()), Color(GetUserThemeColor())
{
	Code = this->Legacy ? kLegacyGlyphFirst : kModernGlyphFirst;
}

void ProgressRing::Init()
{
	Code = Legacy ? kLegacyGlyphFirst : kModernGlyphFirst;
}

void ProgressRing::Start()
{
	if (Legacy)
		SetTimer(reinterpret_cast<UINT_PTR>(this), kLegacyFrameIntervalMs, reinterpret_cast<TIMERPROC>(PRTimerProc<true>));
	else
		SetTimer(reinterpret_cast<UINT_PTR>(this), kModernFrameIntervalMs, reinterpret_cast<TIMERPROC>(PRTimerProc<false>));
}

void ProgressRing::Stop()
{
	KillTimer(reinterpret_cast<UINT_PTR>(this));
}

void ProgressRing::OnDraw(HDC hdc, RECT rect)
{
	SetTextColor(hdc, Color);
	DrawText(hdc, &Code, 1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void ProgressRing::OnSize(BYTE type, int nClientWidth, int nClientHeight, WindowBatchPositioner)
{
	DeleteObject(hFont);
	hFont = CreateFontA(min(nClientWidth, nClientHeight), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_QUALITY, kProgressRingFontName);
	Invalidate();
}

void ProgressRing::OnShowWindow(bool bIsBeingShown, int Status)
{
	if (!bIsBeingShown)
		Stop();
}

void ProgressRing::OnThemeChanged()
{
	Color = GetUserThemeColor();
	Invalidate();
}
