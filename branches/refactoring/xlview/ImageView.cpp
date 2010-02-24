#include <assert.h>
#include <Windows.h>
#include "libxl/include/utilities.h"
#include "libxl/include/Language.h"
#include "libxl/include/ui/Gdi.h"
#include "libxl/include/ui/CtrlMain.h"
#include "ImageView.h"
#include "MainWindow.h"


//////////////////////////////////////////////////////////////////////////
// static
unsigned __stdcall CImageView::_ZoomThread (void *param) {
	CImageView *pThis = (CImageView *)param;
	assert(pThis != NULL);
	HANDLE hEvent = pThis->m_hEvents[0];
	assert(hEvent != NULL);
	for (;;) {
		::WaitForSingleObject(hEvent, INFINITE);
		if (pThis->m_exiting) {
			break;
		}

		xl::CScopeLock lock(pThis);
		if (pThis->m_imageRealSize == NULL) {
			continue; // no source
		}
		bool suitable = pThis->m_suitable;
		CSize szRS = pThis->m_imageRealSize->getImageSize();
		CSize szZoomTo = pThis->m_szZoom;
		CHECK_ZOOM_SIZE(szZoomTo);
		if (pThis->m_imageZoomed && pThis->m_imageZoomed->getImageSize() == szZoomTo) {
			continue; // zoom not needed
		}
		if (szZoomTo.cx * 2 >= szRS.cx || szZoomTo.cy * 2 >= szRS.cy) {
			pThis->m_imageZoomed = pThis->m_imageRealSize;
			pThis->invalidate();
			continue; // zoomed too large, so we use the real size image instead
		}

		int index = pThis->m_pImageManager->getCurrIndex();
		CImagePtr imageRS = pThis->m_imageRealSize;
		pThis->m_zooming = true;
		lock.unlock();

		CImagePtr imageZoomed = imageRS->resize(szZoomTo.cx, szZoomTo.cy, true);

		lock.lock(pThis);
		pThis->m_zooming = false;
		if (index == pThis->m_pImageManager->getCurrIndex()) {
			pThis->m_imageZoomed = imageZoomed;
			pThis->invalidate();
			lock.unlock();

			if (suitable) {
				pThis->m_pImageManager->setCurrentSuitableImage(imageZoomed, szRS, index);
			}

			lock.lock(pThis);
			imageZoomed.reset(); // avoid race condition 
			lock.unlock();
		} else {
			lock.unlock();
		}
	}

	return 0;
}

//////////////////////////////////////////////////////////////////////////
// protected
void CImageView::_OnIndexChanged (int index) {
	assert(getLockLevel() > 0); // must be called in lock
	assert(m_pImageManager != NULL);

	_ResetDisplayInfo();

	if (index == m_pImageManager->getCurrIndex()) {
		CCachedImagePtr cachedImage = m_pImageManager->getCurrentCachedImage();
		m_imageZoomed = cachedImage->getCachedImage();
		if (m_imageZoomed != NULL) {
			assert(cachedImage->getImageSize() != CSize(-1, -1));
			m_szReal = cachedImage->getImageSize();
			CRect rc = getClientRect();
			m_szDisplay = CImage::getSuitableSize(CSize(rc.Width(), rc.Height()), m_szReal);
			_NotifyDisplayChanged();
		}
	}
	invalidate();
}

void CImageView::_OnImageLoaded (CImagePtr image) {
	assert(getLockLevel() > 0); // must be called in lock
	assert(m_pImageManager != NULL);

	m_imageRealSize = image;
	if (m_szDisplay == CSize(-1, -1)) {
		m_szReal = image->getImageSize();
		CRect rc = getClientRect();
		if (rc.Width() <= 0 || rc.Height() <= 0) {
			invalidate();
			return;
		}

		assert(m_suitable);
		m_szDisplay = CImage::getSuitableSize(CSize(rc.Width(), rc.Height()), m_szReal);
		_NotifyDisplayChanged();
		invalidate();
	}
	m_szZoom = m_szDisplay;
	_BeginZoom();
}

CRect CImageView::_CalcDisplayArea (CRect rcView, CSize szDisplay, CPoint ptSrc) {
	int dx = (rcView.Width() - szDisplay.cx) / 2;
	int dy = (rcView.Height() - szDisplay.cy) / 2;
	int dw = szDisplay.cx - ptSrc.x;
	int dh = szDisplay.cy - ptSrc.y;
	if (dx < 0) {
		dx = 0;
		dw = rcView.Width();
	} else {
		assert(ptSrc.x == 0);
	}
	if (dy < 0) {
		dy = 0;
		dh = rcView.Height();
	} else {
		assert(ptSrc.y == 0);
	}
	dx += rcView.left;
	dy += rcView.top;

	return CRect(dx, dy, dx + dw, dy + dh);
}

void CImageView::_SetDisplaySize (CRect rcView, CSize szDisplay, CPoint ptCur) {
	assert(getLockLevel() > 0);
	CRect rcDisplayAreaBefore = _CalcDisplayArea(rcView, m_szDisplay, m_ptSrc);
	if (!rcDisplayAreaBefore.PtInRect(ptCur)) {
		ptCur = CPoint(rcView.left + rcView.Width() / 2, rcView.top + rcView.Height() / 2);
	}
	CSize szDisplayBefore = m_szDisplay;
	assert(szDisplayBefore.cx > 0 && szDisplayBefore.cy > 0);
	m_szDisplay = szDisplay;

	// adjust m_ptSrc
	if (rcView.Width() >= m_szDisplay.cx) {
		m_ptSrc.x = 0;
	} else {
		int x = m_szDisplay.cx * (ptCur.x - rcDisplayAreaBefore.left + m_ptSrc.x) / szDisplayBefore.cx;
		m_ptSrc.x = x + rcView.left - ptCur.x;
	}
	if (rcView.Height() >= m_szDisplay.cy) {
		m_ptSrc.y = 0;
	} else {
		int y = m_szDisplay.cy * (ptCur.y - rcDisplayAreaBefore.top + m_ptSrc.y) / szDisplayBefore.cy;
		m_ptSrc.y = y + rcView.top - ptCur.y;
	}
	_CheckPtSrc(m_ptSrc);

	_NotifyDisplayChanged();
}

void CImageView::_ResetDisplayInfo () {
	assert(getLockLevel() > 0); // must be locked
	m_szDisplay = CSize(-1, -1);
	m_szReal = CSize(-1, -1);
	m_szZoom = CSize(-1, -1);
	m_suitable = true;
	m_ptSrc = CPoint(0, 0);
	m_imageRealSize.reset();
	m_imageZoomed.reset();
#ifdef PROGRESS_ZOOMING
	m_ptCurSaved = CPoint(-1, -1);
#endif

	_NotifyDisplayChanged();
}

void CImageView::_CheckPtSrc (CPoint &ptSrc) {
	int x = ptSrc.x;
	int y = ptSrc.y;

	CRect rc = getClientRect();
	int w = rc.Width();
	int h = rc.Height();

	if (x > m_szDisplay.cx - w) {
		x = m_szDisplay.cx - w;
	}
	if (x < 0) {
		x = 0;
	}
	if (y > m_szDisplay.cy - h) {
		y = m_szDisplay.cy - h;
	}
	if (y < 0) {
		y = 0;
	}

	if (ptSrc.x != x || ptSrc.y != y) {
		ptSrc = CPoint(x, y);
	}
}

void CImageView::_NotifyDisplayChanged () {
	CRect rc = getClientRect();
	CSize szDisplay = m_szDisplay;
	CSize szView(rc.Width(), rc.Height());
	CPoint ptSrc = m_ptSrc;

	// TODO:
	// notify others with: 1. szDisplay; 2. szView; 3. ptSrc
	// and other info can be calculated by the three parameters
}

void CImageView::_CalculateZoomedSize (CSize &szDisplay, CSize szReal, bool isZoomin, double factor) {
	double x, y;
	if (szReal.cx > szReal.cy) {
		double cx = szReal.cx / 100.0;
		if (cx < (double)szDisplay.cx * factor) {
			cx = (double)szDisplay.cx * factor;
		}
		
		if (isZoomin) {
			x = szDisplay.cx + (int)cx;
		} else {
			x = szDisplay.cx - (int)cx;
			if (x < 1) {
				x = 1;
			}
		}
		double ratio = x / szReal.cx;
		if (ratio > 0.95 && ratio < 1.05) {
			x = szReal.cx;
			y = szReal.cy;
		} else {
			y = (int)(x * szReal.cy / szReal.cx);
			if (y < 1) {
				y = 1;
			}
		}
	} else {
		double cy = szReal.cy / 100.0;
		if (cy < (double)szDisplay.cy * factor) {
			cy = (double)szDisplay.cy * factor;
		}

		if (isZoomin) {
			y = szDisplay.cy + cy;
		} else {
			y = szDisplay.cy - cy;
			if (y < 1) {
				y = 1;
			}
		}
		double ratio = y / szReal.cy;
		if (ratio > 0.95 && ratio < 1.05) {
			x = szReal.cx;
			y = szReal.cy;
		} else {
			x = (int)(y * szReal.cx / szReal.cy);
			if (x < 1) {
				x = 1;
			}
		}
	}
	szDisplay = CSize((int)x, (int)y);
}

#ifdef PROGRESS_ZOOMING
bool CImageView::_CalcStepedDisplaySize (CSize &szDisplay, CSize szZoom) {
	assert(getLockLevel() > 0);
	if (szDisplay == szZoom) {
		return false; // not needed
	}

	double deltaX = (double)szZoom.cx - m_szDisplayBegin.cx;
	double deltaY = (double)szZoom.cy - m_szDisplayBegin.cy;
	m_step ++;
	deltaX = deltaX * m_step / 20;
	deltaY = deltaY * m_step / 20;
	szDisplay.cx += deltaX;
	szDisplay.cy += deltaY;

	assert(szZoom.cx > 0);
	double ratio = (double)szDisplay.cx / (double)szZoom.cx;
	if (ratio >= 0.95 && ratio <= 1.05) {
		szDisplay = szZoom;
	}

	return true;
}
#endif

void CImageView::_BeginZoom () {
	_RunThread(0);
}


//////////////////////////////////////////////////////////////////////////
// public

CImageView::CImageView (CImageManager *pImageManager)
	: xl::ui::CControl(ID_VIEW)
	, m_pImageManager(pImageManager)
	, m_szReal(-1, -1)
	, m_szDisplay(-1, -1)
	, m_szZoom(-1, -1)
	, m_ptSrc(0, 0)
	, m_suitable(true)
	, m_zooming(false)
	, m_ptCapture(-1, -1)
#ifdef PROGRESS_ZOOMING
	, m_ptCurSaved(-1, -1)
#endif
	, m_hCurNormal(::LoadCursor(NULL, IDC_ARROW))
	, m_hCurMove(::LoadCursor(NULL, IDC_SIZEALL))
	, m_exiting(false)
{
	setStyle(_T("px:left;py:top;width:fill;height:fill;padding:2 2 16 2;"));
	// setStyle(_T("background-color:#808080;"));
	setStyle(_T("background-color:#202020;"));

	m_pImageManager->subscribe(this);

	_CreateThreads();
}

CImageView::~CImageView (void) {
	_TerminateThreads();
}

void CImageView::showSuitable (CPoint ptCur) {
	xl::CScopeLock lock(this);
	if (m_szReal == CSize(-1, -1) || m_suitable) {
		return;
	}

	m_suitable = true;

	CRect rc = getClientRect();
	CSize szArea(rc.Width(), rc.Height());
	CSize szDisplay = CImage::getSuitableSize(szArea, m_szReal, true);
	_SetDisplaySize(rc, szDisplay);
	m_szZoom = szDisplay;
	CHECK_ZOOM_SIZE(m_szZoom);
	// create an image for fast display if ...
	if (m_imageZoomed == m_imageRealSize) {
		assert(m_imageZoomed != NULL);
		m_imageZoomed = m_pImageManager->getCurrentCachedImage()->getCachedImage();
	}

	_BeginZoom();
	invalidate();
}

void CImageView::showRealSize (CPoint ptCur) {
	xl::CScopeLock lock(this);
	if (m_szReal == CSize(-1, -1) || m_szDisplay == m_szReal) {
		return;
	}
	m_suitable = false;

	CRect rc = getClientRect();
	m_szZoom = m_szReal;
	_BeginZoom();

#ifdef PROGRESS_ZOOMING
	m_szDisplayBegin = m_szDisplay;
	m_step = 0;
	CSize szDisplay = m_szDisplay;
	CSize szZoom = m_szZoom;
	
	_CalcStepedDisplaySize(szDisplay, szZoom);
	_SetDisplaySize(rc, szDisplay, ptCur);
	m_ptCurSaved = ptCur;
	_SetTimer(100, ID_VIEW);
#else
	_SetDisplaySize(rc, m_szReal, ptCur);
#endif
	invalidate();
}

void CImageView::showLarger (CPoint ptCur) {
	xl::CScopeLock lock(this);
	if (m_szReal == CSize(-1, -1)) {
		return;
	}
	m_suitable = false;

	CSize szDisplay = m_szDisplay;
	_CalculateZoomedSize(szDisplay, m_szReal, true, 0.15);
	m_szZoom = szDisplay;
	_BeginZoom();

	CRect rc = getClientRect();
	_SetDisplaySize(rc, szDisplay, ptCur);
	invalidate();
}

void CImageView::showSmaller (CPoint ptCur) {
	xl::CScopeLock lock(this);
	if (m_szReal == CSize(-1, -1)) {
		return;
	}
	m_suitable = false;

	CSize szDisplay = m_szDisplay;
	_CalculateZoomedSize(szDisplay, m_szReal, false, 0.15);
	m_szZoom = szDisplay;
	_BeginZoom();

	CRect rc = getClientRect();
	_SetDisplaySize(rc, szDisplay, ptCur);
	invalidate();
}

void CImageView::onSize () {
	xl::CTimerLogger logger(_T("onSize "));
	assert(m_pImageManager != NULL);
	CRect rc = getClientRect();
	m_pImageManager->onViewSizeChanged(rc);

	lock();
	if (m_imageRealSize != NULL) {
		if (m_suitable) {
			assert(m_szReal != CSize(-1, -1));
			CSize szArea(rc.Width(), rc.Height());
			m_szDisplay = CImage::getSuitableSize(szArea, m_szReal, true);
			m_szZoom = m_szDisplay;

			_NotifyDisplayChanged();

			_BeginZoom();
		} else {
			_CheckPtSrc(m_ptSrc);
			_NotifyDisplayChanged();
		}
	}
	unlock();
	invalidate();
}

void CImageView::drawMe (HDC hdc) {
	assert(m_pImageManager != NULL);
	xl::CTimerLogger logger(_T("drawMe "));
	DWORD tick = ::GetTickCount();

	xl::CScopeLock lock(this);
	CRect rc = getClientRect();
	CSize szDisplay = m_szDisplay;
	if (rc.Width() <= 0 || rc.Height() <= 0 || m_szReal == CSize(-1, -1) || szDisplay.cx <= 0 || szDisplay.cy <= 0) {
		return;
	}
	CImagePtr image = m_imageZoomed;
	if (image == NULL) {
		return;
	}
	CSize szImage = image->getImageSize();
	CPoint ptSrc = m_ptSrc;
	lock.unlock();

	// XLTRACE(_T("image size (%d - %d)\n"), szImage.cx, szImage.cy);
	CRect rcDisplayArea = _CalcDisplayArea(rc, szDisplay, ptSrc);


	xl::ui::CDCHandle dc(hdc);
	xl::ui::CDC mdc;
	mdc.CreateCompatibleDC(dc);
	xl::ui::CDIBSectionHelper dibHelper(image->getImage(0), mdc);

	if (szImage == szDisplay) {
		// use BitBlt
		dc.BitBlt(rcDisplayArea.left, rcDisplayArea.top, rcDisplayArea.Width(), rcDisplayArea.Height(), 
			mdc, ptSrc.x, ptSrc.y, SRCCOPY);
		XLTRACE(_T("Use BitBlt()\n"));
	} else {
		// use StretchBlt
		int sx = szImage.cx * ptSrc.x / szDisplay.cx;
		int sy = szImage.cy * ptSrc.y / szDisplay.cy;
		int sw = szImage.cx * rcDisplayArea.Width() / szDisplay.cx;
		int sh = szImage.cy * rcDisplayArea.Height() / szDisplay.cy;
		lock.lock(this);
		int oldMode = dc.SetStretchBltMode(m_zooming ? COLORONCOLOR : HALFTONE);
		dc.StretchBlt(rcDisplayArea.left, rcDisplayArea.top, rcDisplayArea.Width(), rcDisplayArea.Height(),
			mdc, sx, sy, sw, sh, SRCCOPY);
		dc.SetStretchBltMode(oldMode);
		lock.unlock();
	}

	dibHelper.detach();

	// free the image
	lock.lock(m_pImageManager);
	image.reset();
	lock.unlock();
}

void CImageView::onLButtonDown (CPoint pt, xl::uint key) {
	CRect rc = getClientRect();
	if (m_szDisplay.cx > rc.Width() || m_szDisplay.cy > rc.Height()) {
		::SetCursor(m_hCurMove);
		_Capture(true);
		m_ptCapture = pt;
		m_ptCaptureSrc = m_ptSrc;
	}
}

void CImageView::onLButtonUp (CPoint pt, xl::uint key) {
	if (m_ptCapture != CPoint(-1, -1)) {
		::SetCursor(m_hCurNormal);
		_Capture(false);
		m_ptCapture = CPoint(-1, -1);
	}
}

void CImageView::onMouseMove (CPoint pt, xl::uint key) {
	if (m_ptCapture != CPoint(-1, -1)) {
		::SetCursor(m_hCurMove);
		int x = m_ptCaptureSrc.x - (pt.x - m_ptCapture.x);
		int y = m_ptCaptureSrc.y - (pt.y - m_ptCapture.y);
		CPoint ptSrc = CPoint(x, y);
		_CheckPtSrc(ptSrc);

		if (m_ptSrc != ptSrc) {
			m_ptSrc = ptSrc;

			_NotifyDisplayChanged();
			
			invalidate();
		}
	} else {
		::SetCursor(m_hCurNormal);
	}
}

void CImageView::onTimer (xl::uint id) {
	xl::CScopeLock lock(this);
#ifdef PROGRESS_ZOOMING
	if (m_ptCurSaved == CPoint(-1, -1) || m_szDisplay == m_szZoom) {
		m_ptCurSaved = CSize(-1, -1);
		return;
	}

	CSize szDisplay = m_szDisplay;
	CSize szZoom = m_szZoom;
	if (_CalcStepedDisplaySize(szDisplay, szZoom)) {
		_SetTimer(100, ID_VIEW);
	}
	_SetDisplaySize(getClientRect(), szDisplay, m_ptCurSaved);

	invalidate();
#endif
}

void CImageView::onLostCapture () {
	m_ptCapture = CPoint(-1, -1);
}


void CImageView::onEvent (EVT evt, void *param) {
	assert(m_pImageManager->getLockLevel() > 0);

	xl::CScopeLock lock(this);

	switch (evt) {
	case CImageManager::EVT_INDEX_CHANGED:
		assert(param);
		_OnIndexChanged(*(int *)param);
		break;
	case CImageManager::EVT_IMAGE_LOADED:
		assert(param);
		_OnImageLoaded(*(CImagePtr *)param);
		break;
	case CImageManager::EVT_FILELIST_READY:
		break;
	default:
		assert(false);
		break;
	}
}


//////////////////////////////////////////////////////////////////////////
// used by ClassWithThreads
const xl::tchar* CImageView::getThreadName() {
	return _T("xlview::CImageView");
}

void CImageView::assignThreadProc() {
	m_procThreads[THREAD_ZOOM] = &_ZoomThread;
}

void CImageView::markThreadExit() {
	m_exiting = true;
}	