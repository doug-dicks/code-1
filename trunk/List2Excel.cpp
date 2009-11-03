// List2Excel.cpp: implementation of the CList2Excel class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "VirtualManLib.h"
#include "List2Excel.h"

#include "D_Progress.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

CList2Excel::CList2Excel()
{
	
}

CList2Excel::~CList2Excel()
{
	
}

////////////////////////////////////////////////////////////////////////////////
//    void ExportListToExcel(CListCtrl* pList, CString strTitle,CString strExcFile)
//    参数：
//        pList        需要导出的List控件指针
//        strTitle     导出的数据表标题
//	  strExcFile   保存的文件名和路径
//    说明:
//        导出CListCtrl控件的全部数据到Excel文件。Excel文件名由用户通过“另存为”
//        对话框输入指定。创建名为strTitle的工作表，将List控件内的所有数据（包括
//        列名和数据项）以文本的形式保存到Excel工作表中。保持行列关系。
/////////////////////////////////////////////////////////////////////////////////

void CList2Excel::ExportListToExcel(CListCtrl* pList, CString strTitle,CString strExcFile)
{
	CString warningStr;
	if (pList->GetItemCount ()>0)
	{    
		CDatabase database;
		CString sDriver = "MICROSOFT EXCEL DRIVER (*.XLS)"; // Excel安装驱动
		CString sExcelFile = strExcFile; // 要建立的Excel文件
		CString sSql;
		CString strField = "",strField2 = "";
		
		int m;
		int n = pList->GetItemCount();
		// 创建进行存取的字符串
		sSql.Format("DRIVER={%s};DSN='';FIRSTROWHASNAMES=1;READONLY=FALSE;CREATE_DB=\"%s\";DBQ=%s",sDriver, sExcelFile, sExcelFile);
		
		// 创建数据库 (既Excel表格文件)
		if( database.OpenEx(sSql,CDatabase::noOdbcDialog) )
		{
			// 创建表结构
			int i;
			LVCOLUMN columnData;
			CString columnName;
			int columnNum = 0;
			CString strH;
			CString strV;
			
			sSql = "";
			strH = "";
			columnData.mask = LVCF_TEXT;
			columnData.cchTextMax =256;
			columnData.pszText = columnName.GetBuffer (256);
			for(i=0;pList->GetColumn(i,&columnData);i++)
			{
				strField = strField + "[" + columnData.pszText +"]" + " char(255), ";
				strField2 = strField2 + "[" + columnData.pszText +"], ";
			}
			columnName.ReleaseBuffer ();
			m = i; 
			// 创建表结构
			sSql.Format("CREATE TABLE [%s] (",strTitle);
			
			strField.Delete(strField.GetLength()-2, 2);
			
			strField += ")";
			sSql += strField;
			database.ExecuteSQL(sSql);
			
			
			strField2.Delete(strField2.GetLength()-2, 2);
			

			//Add By HuangXiao Ke；把导出时进度信息显示出来，耦合性太大，如果别的程序要用这个累，把这些无关代码去掉

			CD_Progress* pDlg = (CD_Progress*)g_dlgQueryManager.GetDialog(IDD_DIALOG_PROGRESS);
			pDlg->ShowWindow(SW_SHOW);
			pDlg->m_cProgress.SetRange(0,n);
			pDlg->m_cProgress.SetPos(0);


			CString str;
			// 插入数值
			for (i=0; i<n; i++)
			{

				pDlg->m_cProgress.SetPos(i);

				sSql="";
				sSql.Format("INSERT INTO [%s] (",strTitle);
				sSql += strField2;
				sSql += ") VALUES (";
				for (int j = 0; j <m ; j++)
				{
					str = "";
					str.Format("'%s', ", pList->GetItemText(i,j));
					sSql = sSql + str;
				}
				sSql.Delete(sSql.GetLength()-2, 2);
				sSql += ")";
				
				//OutputDebugString(sSql);
				
				database.ExecuteSQL(sSql);	    
			}

			pDlg->ShowWindow(SW_HIDE);
		} 

		// 关闭数据库
		database.Close();
		warningStr.Format("导出文件保存于%s.xls!",sExcelFile);
		AfxMessageBox(warningStr);
	}
}

void CList2Excel::ExportListToExcelTab(CListCtrl* pList, CString strTitle,CString strExcFile)
{
	CString warningStr;
	if (pList->GetItemCount ()>0)
	{    
		int nTabCount = pList->GetItemCount ()/65000 + 1;
		int nItemCount = pList->GetItemCount ();
		int nCurExcelCount = 0;
		CString sExcelFile = strExcFile; // 要建立的Excel文件

		CD_Progress* pDlg = (CD_Progress*)g_dlgQueryManager.GetDialog(IDD_DIALOG_PROGRESS);
		pDlg->ShowWindow(SW_SHOW);
		pDlg->m_cProgress.SetRange(0,nItemCount);
		pDlg->m_cProgress.SetPos(0);

		for (int nCurCount = 1; nCurCount <= nTabCount; nCurCount++)
		{
			CDatabase database;
			CString sDriver = "MICROSOFT EXCEL DRIVER (*.XLS)"; // Excel安装驱动
			//CString sExcelFile = strExcFile; // 要建立的Excel文件
			CString sSql;
			CString strField = "",strField2 = "";
			
			int m;
			int n = pList->GetItemCount();
			// 创建进行存取的字符串
			sSql.Format("DRIVER={%s};DSN='';FIRSTROWHASNAMES=1;READONLY=FALSE;CREATE_DB=\"%s\";DBQ=%s",sDriver, sExcelFile, sExcelFile);
			
			// 创建数据库 (既Excel表格文件)
			if( database.OpenEx(sSql,CDatabase::noOdbcDialog) )
			{
				// 创建表结构
				int i;
				LVCOLUMN columnData;
				CString columnName;
				int columnNum = 0;
				CString strH;
				CString strV;
				
				sSql = "";
				strH = "";
				columnData.mask = LVCF_TEXT;
				columnData.cchTextMax =256;
				columnData.pszText = columnName.GetBuffer (256);
				for(i=0;pList->GetColumn(i,&columnData);i++)
				{
					strField = strField + "[" + columnData.pszText +"]" + " char(255), ";
					strField2 = strField2 + "[" + columnData.pszText +"], ";
				}
				columnName.ReleaseBuffer ();
				m = i; 
				// 创建表结构
				
				CString strTmp ="";
				strTmp.Format("%d",nCurCount);
				sSql.Format("CREATE TABLE [%s] (",strTitle+strTmp);
				OutputDebugString(sSql);
				strField.Delete(strField.GetLength()-2, 2);
				
				strField += ")";
				sSql += strField;
				database.ExecuteSQL(sSql);
				
				
				strField2.Delete(strField2.GetLength()-2, 2);
				
				
				//Add By HuangXiao Ke；把导出时进度信息显示出来，耦合性太大，如果别的程序要用这个累，把这些无关代码去掉
				

				
				
				CString str;
				int nCurTab = 1;
				CString	strCurTab = "1";
				// 插入数值
				for (; nCurExcelCount<nItemCount; nCurExcelCount++)
				{
					
					pDlg->m_cProgress.SetPos(nCurExcelCount);
										
					sSql="";
					sSql.Format("INSERT INTO [%s] (",strTitle+strTmp);
					sSql += strField2;
					sSql += ") VALUES (";
					for (int j = 0; j <m ; j++)
					{
						str = "";
						str.Format("'%s', ", pList->GetItemText(nCurExcelCount,j));
						sSql = sSql + str;
					}
					sSql.Delete(sSql.GetLength()-2, 2);
					sSql += ")";
					
					database.ExecuteSQL(sSql);	
					
					if ((nCurExcelCount+1)%65000 == 0)
					{
						nCurExcelCount++;
						break;
					}
				}
				

			} 
			
			// 关闭数据库
			database.Close();
		}
		pDlg->ShowWindow(SW_HIDE);
		warningStr.Format("导出文件保存于%s.xls!",sExcelFile);
		AfxMessageBox(warningStr);
	}
}