#ifndef XL_VIEW_IMAGE_H
#define XL_VIEW_IMAGE_H
#include <vector>
#include <memory>
#include <limits>
#include <atltypes.h>
#include "libxl/include/common.h"
#include "libxl/include/string.h"
#include "libxl/include/ui/DIBSection.h"


class CImage;
typedef std::tr1::shared_ptr<CImage>    CImagePtr;

enum {
	THUMBNAIL_WIDTH = 32,
	THUMBNAIL_HEIGHT = 32,
};


//////////////////////////////////////////////////////////////////////////
// CImage

class CImage
{
protected:
	struct Frame {
		enum {
			DELAY_INFINITE = 0
		};
		xl::ui::CDIBSectionPtr bitmap;
		xl::uint delay;

		Frame ();
		~Frame ();
	};

	typedef std::tr1::shared_ptr<Frame>            _FramePtr;
	typedef std::vector<_FramePtr>                 _FrameContainer;

	_FrameContainer                                m_bads;
	int                                            m_width;
	int                                            m_height;

	// void _CreateThumbnail ();

public:
	//////////////////////////////////////////////////////////////////////////
	// interface for canceling image loading
	class ICancel {
	public:
		virtual bool cancelLoading () = 0;
	};

	CImage ();
	virtual ~CImage ();

	bool load (const xl::tstring &file, ICancel *pCancel = NULL);
	void operator = (const CImage &);
	CImagePtr clone ();
	void clear ();

	xl::uint getImageCount () const;
	CSize getImageSize () const { return CSize(m_width, m_height); }
	int getImageWidth () const { return m_width; }
	int getImageHeight () const { return m_height; }

	xl::uint getImageDelay (xl::uint index) const;
	xl::ui::CDIBSectionPtr getImage (xl::uint index);

	void insertImage (xl::ui::CDIBSectionPtr bitmap, xl::uint delay);
	CImagePtr resize (int width, int height, bool usehalftone = true);

	static CSize getSuitableSize (CSize szArea, CSize szImage, bool dontEnlarge = true);
};


//////////////////////////////////////////////////////////////////////////
// CDisplayImage
class CDisplayImage;
typedef std::tr1::shared_ptr<CDisplayImage>            CDisplayImagePtr;

class CDisplayImage
{
	mutable CRITICAL_SECTION                       m_cs;
	bool               m_locked;
	bool               m_zooming;
	xl::tstring        m_fileName;

	int                m_widthReal;
	int                m_heightReal;

	CImagePtr          m_imgThumbnail;
	CImagePtr          m_imgZoomed;
	CImagePtr          m_imgZooming;
	CImagePtr          m_imgRealSize;

public:
	void lock ();
	void unlock ();

	CDisplayImage (const xl::tstring &fileName);
	virtual ~CDisplayImage ();
	CDisplayImagePtr clone ();

	bool loadZoomed (int width, int height, CImage::ICancel *pCancel = NULL);
	bool loadRealSize (CImage::ICancel *pCancel = NULL);
	bool loadThumbnail (CImage::ICancel *pCancel = NULL);

	void clearThumbnail ();
	void clearRealSize ();
	void clearZoomed ();
	void clear ();

	xl::tstring getFileName () const;
	int getRealWidth () const;
	int getRealHeight () const;

	CImagePtr getThumbnail ();
	CImagePtr getZoomedImage ();
	CImagePtr getRealSizeImage ();
};



#endif
