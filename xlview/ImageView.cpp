#include <assert.h>
#include <Windows.h>
#include "libxl/include/utilities.h"
#include "libxl/include/ui/Gdi.h"
#include "libxl/include/ui/CtrlMain.h"
#include "ImageView.h"
#include "MainWindow.h"

//////////////////////////////////////////////////////////////////////////
// static

unsigned int __stdcall CImageView::_LoadThread (void *param) {
	assert(param);
	CImageView *pThis = (CImageView *)param;
	for (;;) {
		::WaitForSingleObject(pThis->m_semaphoreLoad, INFINITE);
		if (pThis->m_exit) {
			break;
		}

		xl::CSimpleLock lock(&pThis->m_cs);
		pThis->m_loading = true;
		pThis->m_currLoading = pThis->m_currIndex;
		CDisplayImagePtr displayImage = pThis->m_image;
		lock.unlock();
		assert(displayImage != NULL);

		bool loaded = displayImage->loadRealSize(pThis);

		lock.lock(&pThis->m_cs);
		pThis->m_loading = false;
		if (!pThis->cancelLoading()) {
			pThis->_OnImageLoaded(loaded);
		}
		pThis->m_currLoading = -1;
	}
	return 0;
}

unsigned int __stdcall CImageView::_ResizeThread (void *param) {
	assert(param);
	CImageView *pThis = (CImageView *)param;
	for (;;) {
		::WaitForSingleObject(pThis->m_semaphoreResize, INFINITE);
		if (pThis->m_exit) {
			break;
		}
		// Sleep(1000);

		xl::CSimpleLock lock(&pThis->m_cs);
		if (pThis->m_loading) {
			continue;
		}
		pThis->m_resizing = true;
		int currIndex = pThis->m_currIndex;
		CDisplayImagePtr image = pThis->m_image;
		if (pThis->m_image->getZoomedImage()) {
			pThis->m_imageWhenResizing = pThis->m_image->getZoomedImage()->clone();
		}
		// lock.unlock();

		CRect rc = pThis->getClientRect();
		CSize szImage(image->getRealWidth(), image->getRealHeight());
		CSize sz = CImage::getSuitableSize(CSize(rc.Width(), rc.Height()), szImage);

		image->loadZoomed(sz.cx, sz.cy, pThis);

		// lock.lock(&pThis->m_cs);
		pThis->m_resizing = false;
		pThis->m_imageWhenResizing.reset();
		if (pThis->m_currIndex == currIndex) {
			pThis->_OnImageResized();
		}
	}
	return 0;
}


//////////////////////////////////////////////////////////////////////////
// protected

void CImageView::_CreateThreads () {
	assert(m_semaphoreLoad == NULL);
	::InitializeCriticalSection(&m_cs);

	xl::tchar name[128];
	_stprintf_s(name, 128, _T("xlview::imageview::semaphore::load for 0x%08x on %d"), this, ::GetTickCount());
	m_semaphoreLoad = ::CreateSemaphore(NULL, 0, 1, name);
	m_threadLoad = (HANDLE)_beginthreadex(NULL, 0, _LoadThread, this, 0, NULL);

	_stprintf_s(name, 128, _T("xlview::imageview::semaphore::resize for 0x%08x on %d"), this, ::GetTickCount());
	m_semaphoreResize = ::CreateSemaphore(NULL, 0, 1, name);
	m_threadResize = (HANDLE)_beginthreadex(NULL, 0, _ResizeThread, this, 0, NULL);
}

void CImageView::_TerminateThreads () {
	m_exit = true;
	::ReleaseSemaphore(m_semaphoreLoad, 1, NULL);
	::ReleaseSemaphore(m_semaphoreResize, 1, NULL);
	HANDLE handles[] = {m_threadLoad, m_threadResize};
	if (::WaitForMultipleObjects(COUNT_OF(handles), handles, TRUE, 3000) == WAIT_TIMEOUT) {
		for (int i = 0; i < COUNT_OF(handles); ++ i) {
			TerminateThread(handles[i], 0);
			::CloseHandle(handles[i]);
		}
	}
	m_threadResize = m_threadLoad = NULL;

	::DeleteCriticalSection(&m_cs);
}


void CImageView::_ResetParameter () {
	xl::CSimpleLock lock(&m_cs);
	m_suitable = true;
	m_zoomTo = m_zoomNow = 0;
	if (m_image != NULL) {
		m_image->clearRealSize();
	}
	m_image.reset();
}

void CImageView::_PrepareDisplay () {

	xl::CSimpleLock lock(&m_cs);
	m_image = m_pImageManager->getImage(m_currIndex);
	assert(m_image);
	if (m_image->getRealSizeImage() == NULL) {
		_BeginLoad();
	} else if (m_image->getZoomedImage() == NULL) {
		_BeginResize();
	}
}

void CImageView::_BeginLoad () {
	::ReleaseSemaphore(m_semaphoreLoad, 1, NULL);
}

void CImageView::_BeginResize () {
	CRect rc = getClientRect();
	if (rc.Width() <= 0 || rc.Height() <= 0) {
		return;
	}

	int w = m_image->getRealWidth();
	int h = m_image->getRealHeight();
	CSize sz = CImage::getSuitableSize(CSize(rc.Width(), rc.Height()), CSize(w, h));
	if (sz.cx == w && sz.cy == h) {
		return; // not needed
	}
	::ReleaseSemaphore(m_semaphoreResize, 1, NULL);
}


void CImageView::_OnIndexChanged () {
	xl::CSimpleLock lock(&m_cs);
	int newIndex = m_pImageManager->getCurrIndex();
	if (newIndex != m_currIndex) {
		m_currIndex = newIndex;
		_ResetParameter();
		_PrepareDisplay();
		invalidate();
	}
}

void CImageView::_OnImageLoaded (bool success) {
	xl::ui::CCtrlMain *pCtrlMain = _GetMainCtrl();
	assert(pCtrlMain);
	ATL::CWindow *pWindow = pCtrlMain->getWindow();
	assert(pWindow);
	xl::CSimpleLock lock(&m_cs);

	if (m_image->getZoomedImage() == NULL) {
		_BeginResize();
		invalidate();
	} else {
		invalidate();
	}
}

void CImageView::_OnImageResized () {
	assert(m_image->getZoomedImage()->getImageCount() > 0);
	invalidate();
}

//////////////////////////////////////////////////////////////////////////

CImageView::CImageView (CImageManager *pImageManager)
	: xl::ui::CControl(ID_VIEW)
	, m_pImageManager(pImageManager)
	, m_currIndex(-1)
	, m_exit(false)
	, m_loading(false)
	, m_resizing(false)
	, m_semaphoreLoad(NULL)
	, m_threadLoad(NULL)
{
	_CreateThreads();

	assert(m_pImageManager != NULL);
	m_currIndex = m_pImageManager->getCurrIndex();
	m_pImageManager->subscribe(this);

	_ResetParameter();

	setStyle(_T("px:left;py:top;width:fill;height:fill;padding:10;"));
	setStyle(_T("background-color:#808080;"));
}

CImageView::~CImageView (void) {
	_TerminateThreads();
}

void CImageView::onSize () {
	if (!m_suitable) {
		return;
	}
	CRect rc = getClientRect();
	if (rc.Width() > 0 && rc.Height() > 0) {
		assert(m_pImageManager);
		m_pImageManager->onViewSizeChanged(rc);
		_BeginResize();
	}
}

void CImageView::drawMe (HDC hdc) {
	CRect rc = getClientRect();

	xl::CSimpleLock lock(&m_cs);
	if (m_image == NULL) {
		return;
	}
	CImagePtr zoomedImage = m_image->getZoomedImage();
	CImagePtr realsizeImage = m_image->getRealSizeImage();
	if (zoomedImage == NULL && realsizeImage == NULL) {
		return;
	}
	CImagePtr drawImage;

	if (m_resizing && m_imageWhenResizing) {
		assert(m_imageWhenResizing && m_imageWhenResizing->getImageCount() > 0);
		drawImage = m_imageWhenResizing;
	} else if (zoomedImage && zoomedImage->getImageCount() > 0) {
		drawImage = zoomedImage;
	} else if (realsizeImage && realsizeImage->getImageCount() > 0 && !m_loading) {
		drawImage = realsizeImage;
	}

	if (drawImage != NULL) {
		xl::ui::CDIBSectionPtr bitmap = drawImage->getImage(0);

		xl::ui::CDCHandle dc(hdc);
		xl::ui::CDC mdc;
		mdc.CreateCompatibleDC(hdc);
		HBITMAP oldBmp = mdc.SelectBitmap(*bitmap);

		int w = m_image->getRealWidth(); // bitmap->getWidth();
		int h = m_image->getRealHeight(); // bitmap->getHeight();
		assert(w > 0 && h > 0);

		if (m_suitable) {
			CSize sz = CImage::getSuitableSize(CSize(rc.Width(), rc.Height()), CSize(w, h));
			w = sz.cx;
			h = sz.cy;
		} else {
			assert(false); // TODO
		}

		int x = (rc.Width() - w) / 2;
		int y = (rc.Height() - h) / 2;
		if (w > rc.Width()) {
			w = rc.Width();
		}
		if (h > rc.Height()) {
			h = rc.Height();
		}
		if (x < 0) {
			x = 0;
		}
		if (y < 0) {
			y = 0;
		}
		x += rc.left;
		y += rc.top;

		if (w != drawImage->getImageWidth() || h != drawImage->getImageHeight()) {
			int oldMode = dc.SetStretchBltMode(COLORONCOLOR);
			dc.StretchBlt(x, y, w, h, mdc, 0, 0, drawImage->getImageWidth(), drawImage->getImageHeight(), SRCCOPY);
			dc.SetStretchBltMode(oldMode);
		} else {
			dc.BitBlt(x, y, w, h, mdc, 0, 0, SRCCOPY);
		}

		mdc.SelectBitmap(oldBmp);
	}
}

void CImageView::onLButtonDown (CPoint pt, xl::uint key) {
}

void CImageView::onLButtonUp (CPoint pt, xl::uint key) {
}

void CImageView::onMouseMove (CPoint pt, xl::uint key) {
}

void CImageView::onLostCapture () {
}


void CImageView::onEvent (EVT evt, void *param) {
	switch (evt) {
	case CImageManager::EVT_INDEX_CHANGED:
		_OnIndexChanged();
		break;
	default:
		break;
	}
}

bool CImageView::cancelLoading () {
	return (m_currIndex != m_currLoading) || m_exit;
}