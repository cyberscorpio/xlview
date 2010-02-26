#ifndef XL_VIEW_IMAGE_MANAGER_H
#define XL_VIEW_IMAGE_MANAGER_H
#include <vector>
#include <atltypes.h>
#include "libxl/include/common.h"
#include "libxl/include/string.h"
#include "libxl/include/utilities.h"
#include "libxl/include/dp/Observable.h"
#include "ClassWithThreads.h"
#include "ImageConfig.h"
#include "CachedImage.h"
#include "ImageLoader.h"


class CImageManager 
	: public xl::dp::CObserableT<CImageManager>
	, public xl::CUserLock
	, public ClassWithThreadT<CImageManager, 2>
{
	friend class ClassWithThreadT<CImageManager, 2>;

	// callbacks
	class CImageLoadCallback : public IImageOperateCallback {
	public:
		bool *m_pExiting;
		bool m_indexChanged;
		bool m_sizeChanged;
		CImageLoadCallback (bool *pExiting) 
			: m_pExiting(pExiting)
			, m_indexChanged(false)
			, m_sizeChanged(false)
		{
			assert(m_pExiting != NULL);
		}

		virtual bool onProgress (int curr, int total) {
			assert(m_pExiting != NULL);
			return !m_indexChanged && !*m_pExiting;
		}
	};

	class CImagePrefetchCallback : public CImageManager::CImageLoadCallback {
	public:
		CImagePrefetchCallback (bool *pExiting) : CImageLoadCallback(pExiting) {

		}

		virtual bool onProgress (int curr, int total) {
			assert(m_pExiting != NULL);
			return !m_indexChanged && !m_sizeChanged && !*m_pExiting;
		}
	};

protected:
	enum DIRECTION {
		FORWARD,
		BACKWARD
	};
	typedef std::vector<xl::uint>                  _Indexes;
	typedef std::vector<CCachedImagePtr>           _CachedImages;
	typedef _CachedImages::iterator                _CachedImageIter;
	xl::tstring        m_directory; // include the last '\\'
	_CachedImages      m_cachedImages;
	xl::uint           m_currIndex;
	DIRECTION          m_direction;

	CSize              m_szPrefetch;

	void _SetIndexNoLock (int index); // called when already locked

	// static
	static void _GetPrefetchIndexes (_Indexes &indexes, int currIndex, int count, DIRECTION direction, int range);

	//////////////////////////////////////////////////////////////////////////
	// thread related
	enum {
		THREAD_LOAD     = 0,
		THREAD_PREFETCH = 1,
		THREAD_COUNT
	};
	bool                                           m_exiting;
	IImageOperateCallback                         *m_callbacks[THREAD_COUNT];
	static unsigned __stdcall _LoadThread (void *);
	static unsigned __stdcall _PrefetchThread (void *);
	void _BeginLoad ();
	void _BeginPrefetch ();

	//////////////////////////////////////////////////////////////////////////
	// for ClassWithThreads
	void markThreadExit ();
	void assignThreadProc();
	const xl::tchar* getThreadName(); 
public:
	// event
	enum EVENT 
		: xl::dp::CObserableT<CImageManager>::EVT
	{
		EVT_FILELIST_READY,                    // param (pointer to total count)
		EVT_INDEX_CHANGED,                     // param (pointer to the current index)
		EVT_IMAGE_LOADED,                      // param (pointer to the CImagePtr)
		EVT_NUM
	};

	CImageManager ();
	virtual ~CImageManager ();

	int getCurrIndex () const;
	size_t getImageCount () const;

	bool setFile (const xl::tstring &file);
	void setIndex (int index);

	void setCurrentSuitableImage (CImagePtr image, CSize szImage, int curr); // only called by CImageView::_ZoomThread

	CCachedImagePtr getCurrentCachedImage ();
	xl::tstring getCurrentFileName ();

	//////////////////////////////////////////////////////////////////////////
	// To be notified
	void onViewSizeChanged (CRect rc); // called by the view to notify its size changed
};

#endif