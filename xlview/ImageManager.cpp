#include <assert.h>
#include "libxl/include/fs.h"
#include "libxl/include/utilities.h"
#include "ImageManager.h"


//////////////////////////////////////////////////////////////////////////
static xl::tchar* s_extensions[] = {
	_T("jpeg"),
	_T("jpg"),
	_T("jif"),
};

bool CImageManager::_IsFileSupported (const xl::tstring &fileName) {
	size_t index = fileName.rfind(_T('.'));
	if (index != fileName.npos) {
		xl::tstring ext = fileName.substr(index + 1);
		bool match = false;
		for (int i = 0; i < COUNT_OF(s_extensions); ++ i) {
			if (_tcsicmp(ext.c_str(), s_extensions[i]) == 0) {
				match = true;
				break;
			}
		}
		return match;
	}
	return false;
}

// note, don't prefetch the 'current' image, 
// left it to the view to fetch, to avoid conflict
void CImageManager::_BeginPrefetch () {
	if (m_rcView.Width() <= 0 || m_rcView.Height() <= 0) {
		return;
	}
}



//////////////////////////////////////////////////////////////////////////

CImageManager::CImageManager ()
	: m_currIndex ((xl::uint)-1)
	, m_rcView(0, 0, 0, 0)
{

}

CImageManager::~CImageManager () {

}

int CImageManager::getCurrIndex () const {
	return m_currIndex;
}

int CImageManager::getImageCount () const {
	return (int)m_images.size();
}

void CImageManager::setFile (const xl::tstring &file) {
	assert(m_images.size() == 0);
	xl::tstring fileName = xl::file_get_name(file);
	m_directory = xl::file_get_directory(file);
	m_directory += _T("\\");
	xl::tstring pattern = m_directory + _T("*.*");
	xl::CTimerLogger logger(_T("searching files cost"));

	int new_index = -1;

	// find the files
	WIN32_FIND_DATA wfd;
	memset(&wfd, 0, sizeof(wfd));
	HANDLE hFind = ::FindFirstFile(pattern, &wfd);
	if (hFind != INVALID_HANDLE_VALUE) {

		do {
			if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				continue; // skip directory
			}

			xl::tstring name = m_directory + wfd.cFileName;
			if (_IsFileSupported(name)) {
				if (_tcsicmp(wfd.cFileName, fileName) == 0) {
					new_index = (int)m_images.size();
				}
				m_images.push_back(CDisplayImagePtr(new CDisplayImage(name)));
			}

		} while (::FindNextFile(hFind, &wfd));

		::FindClose(hFind);
		
		xl::trace(_T("get %d files\n"), m_images.size());
	} else {
		::MessageBox(NULL, _T("Can not find any image file"), 0, MB_OK);
	}

	if (new_index == -1 && m_images.size() > 0) {
		new_index = 0;
	}
	setIndex(new_index);
}

void CImageManager::setIndex (int index) {
	if (m_currIndex != index) {
		m_currIndex = index;
		_BeginPrefetch();
		_TriggerEvent(EVT_INDEX_CHANGED, NULL);
	}
}

CDisplayImagePtr CImageManager::getImage (int index) {
	assert(index >= 0 && index < getImageCount());
	return m_images[index];
}

void CImageManager::onViewSizeChanged (CRect rc) {
	if (m_rcView == rc) {
		return;
	}

	m_rcView = rc;
	if (m_rcView.Width() <= 0 || m_rcView.Height() <= 0) {
		return;
	}

	_BeginPrefetch();
}