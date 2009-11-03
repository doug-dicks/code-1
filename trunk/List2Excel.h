#ifndef _LIST2EXCEL_H_
#define _LIST2EXCEL_H_

#include <afxdb.h>

class CList2Excel  
{
public:
	CList2Excel();
	virtual ~CList2Excel();
	void ExportListToExcel(CListCtrl* , CString ,CString);
	void ExportListToExcelTab(CListCtrl* pList, CString strTitle,CString strExcFile);
};

#endif
