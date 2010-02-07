#ifndef XLVIEW_IMAGE_VIEW_H
#define XLVIEW_IMAGE_VIEW_H
#include "libxl/include/ui/Control.h"
#include "DisplayImage.h"
#include "ImageLoader.h"
#include "ImageManager.h"

#define ID_VIEW  99

class CImageView 
	: public xl::ui::CControl
	, public CImageManager::IObserver
	, public xl::CUserLock
{
protected:
	CImageManager     *m_pImageManager;

public:
	CImageView(CImageManager *pImageManager);
	virtual ~CImageView(void);

	//////////////////////////////////////////////////////////////////////////
	// virtual

	// xl::ui::CControl
	virtual void onSize ();
	virtual void drawMe (HDC hdc);

	virtual void onLButtonDown (CPoint pt, xl::uint key);
	virtual void onLButtonUp (CPoint pt, xl::uint key);
	virtual void onMouseMove (CPoint pt, xl::uint key);
	virtual void onLostCapture ();

	// CImageManager::IObserver
	virtual void onEvent (EVT evt, void *param);
};

#endif
