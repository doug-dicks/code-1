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
//    ������
//        pList        ��Ҫ������List�ؼ�ָ��
//        strTitle     ���������ݱ����
//	  strExcFile   ������ļ�����·��
//    ˵��:
//        ����CListCtrl�ؼ���ȫ�����ݵ�Excel�ļ���Excel�ļ������û�ͨ�������Ϊ��
//        �Ի�������ָ����������ΪstrTitle�Ĺ�������List�ؼ��ڵ��������ݣ�����
//        ��������������ı�����ʽ���浽Excel�������С��������й�ϵ��
/////////////////////////////////////////////////////////////////////////////////

void CList2Excel::ExportListToExcel(CListCtrl* pList, CString strTitle,CString strExcFile)
{
	CString warningStr;
	if (pList->GetItemCount ()>0)
	{    
		CDatabase database;
		CString sDriver = "MICROSOFT EXCEL DRIVER (*.XLS)"; // Excel��װ����
		CString sExcelFile = strExcFile; // Ҫ������Excel�ļ�
		CString sSql;
		CString strField = "",strField2 = "";
		
		int m;
		int n = pList->GetItemCount();
		// �������д�ȡ���ַ���
		sSql.Format("DRIVER={%s};DSN='';FIRSTROWHASNAMES=1;READONLY=FALSE;CREATE_DB=\"%s\";DBQ=%s",sDriver, sExcelFile, sExcelFile);
		
		// �������ݿ� (��Excel����ļ�)
		if( database.OpenEx(sSql,CDatabase::noOdbcDialog) )
		{
			// ������ṹ
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
			// ������ṹ
			sSql.Format("CREATE TABLE [%s] (",strTitle);
			
			strField.Delete(strField.GetLength()-2, 2);
			
			strField += ")";
			sSql += strField;
			database.ExecuteSQL(sSql);
			
			
			strField2.Delete(strField2.GetLength()-2, 2);
			

			//Add By HuangXiao Ke���ѵ���ʱ������Ϣ��ʾ�����������̫�������ĳ���Ҫ������ۣ�����Щ�޹ش���ȥ��

			CD_Progress* pDlg = (CD_Progress*)g_dlgQueryManager.GetDialog(IDD_DIALOG_PROGRESS);
			pDlg->ShowWindow(SW_SHOW);
			pDlg->m_cProgress.SetRange(0,n);
			pDlg->m_cProgress.SetPos(0);


			CString str;
			// ������ֵ
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

		// �ر����ݿ�
		database.Close();
		warningStr.Format("�����ļ�������%s.xls!",sExcelFile);
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
		CString sExcelFile = strExcFile; // Ҫ������Excel�ļ�

		CD_Progress* pDlg = (CD_Progress*)g_dlgQueryManager.GetDialog(IDD_DIALOG_PROGRESS);
		pDlg->ShowWindow(SW_SHOW);
		pDlg->m_cProgress.SetRange(0,nItemCount);
		pDlg->m_cProgress.SetPos(0);

		for (int nCurCount = 1; nCurCount <= nTabCount; nCurCount++)
		{
			CDatabase database;
			CString sDriver = "MICROSOFT EXCEL DRIVER (*.XLS)"; // Excel��װ����
			//CString sExcelFile = strExcFile; // Ҫ������Excel�ļ�
			CString sSql;
			CString strField = "",strField2 = "";
			
			int m;
			int n = pList->GetItemCount();
			// �������д�ȡ���ַ���
			sSql.Format("DRIVER={%s};DSN='';FIRSTROWHASNAMES=1;READONLY=FALSE;CREATE_DB=\"%s\";DBQ=%s",sDriver, sExcelFile, sExcelFile);
			
			// �������ݿ� (��Excel����ļ�)
			if( database.OpenEx(sSql,CDatabase::noOdbcDialog) )
			{
				// ������ṹ
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
				// ������ṹ
				
				CString strTmp ="";
				strTmp.Format("%d",nCurCount);
				sSql.Format("CREATE TABLE [%s] (",strTitle+strTmp);
				OutputDebugString(sSql);
				strField.Delete(strField.GetLength()-2, 2);
				
				strField += ")";
				sSql += strField;
				database.ExecuteSQL(sSql);
				
				
				strField2.Delete(strField2.GetLength()-2, 2);
				
				
				//Add By HuangXiao Ke���ѵ���ʱ������Ϣ��ʾ�����������̫�������ĳ���Ҫ������ۣ�����Щ�޹ش���ȥ��
				

				
				
				CString str;
				int nCurTab = 1;
				CString	strCurTab = "1";
				// ������ֵ
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
			
			// �ر����ݿ�
			database.Close();
		}
		pDlg->ShowWindow(SW_HIDE);
		warningStr.Format("�����ļ�������%s.xls!",sExcelFile);
		AfxMessageBox(warningStr);
	}
}