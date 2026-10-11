#include "pch.h"

#include <algorithm>

#include "ItemBankWindow.h"
#include "ItemSlot.h"
#include "BasicScrollBarEx.h"
#include "BasicTreeEx.h"
#include "../[Lib]__EngineUI/Sources/BasicTreeNode.h"
#include "BasicLineBox.h"
#include "BasicTextButton.h"
#include "../[Lib]__EngineUI/Sources/BasicButton.h"
#include "../[Lib]__EngineUI/Sources/BasicScrollThumbFrame.h"
#include "../[Lib]__EngineUI/Sources/BasicTextBox.h"
#include "../[Lib]__EngineUI/Sources/UIEditBox.h"
#include "../[Lib]__Engine/Sources/DxTools/DxFontMan.h"
#include "GLGaeaClient.h"
#include "GLItemMan.h"
#include "GLStringTable.h"
#include "InnerInterface.h"
#include "UITextControl.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

namespace
{
	const char* szCLASS[] = { "All", "Brawler", "Swordsman", "Archer", "Shaman", "Extreme" };

	//	Category names the item table does not give (the same as t31k.com/ran/items,
	//	scripts/export_item_catalog.py CATEGORY_NAMES).
	const char* CategoryName ( WORD wMID )
	{
		switch ( wMID )
		{
		case 4:		return "HP Potions";
		case 5:		return "MP Potions";
		case 6:		return "SP Potions";
		case 8:		return "Vitality Potions";
		case 13:	return "Arrows";
		case 14:	return "Script Arrows";
		case 18:	return "Refine Stones";
		case 21:	return "Mid Necklaces";
		case 22:	return "Vitality Potions II";
		case 38:	return "Sacred Gate Female Coat";
		case 41:	return "Sacred Gate Female Pants";
		case 65:	return "Misc Items";
		case 77:	return "Rainbow Weapons";
		case 110:	return "Event Placeholders";
		case 125:	return "Premium Weapons [15D]";
		case 133:	return "Event Etc";
		case 144:	return "Rebuild Cards";
		case 152:	return "Brawler Scrolls II";
		case 153:	return "Swordsman Scrolls II";
		case 154:	return "Archer Scrolls II";
		case 155:	return "Shaman Scrolls II";
		case 156:	return "Supreme Brook Armor";
		case 157:	return "Rare Supreme Brook Armor";
		case 159:	return "Rare Supreme Weapons";
		}
		return NULL;
	}

	//	The tree's top level (export_item_catalog.py group()).
	const char* szGROUP[] = { "Weapons", "Armor", "Accessories", "Potions", "Upgrades", "Skill Scrolls",
							  "Quest", "Premium & Costumes", "Pets & Boards", "Regional", "Misc" };

	int Group ( WORD wMID )
	{
		switch ( wMID )
		{
		case 0: case 3: case 13: case 14: case 19: case 23: case 27: case 28: case 29: case 30: case 31:
		case 68: case 69: case 70: case 74: case 75: case 77: case 79: case 81: case 82:
		case 97: case 98: case 99: case 100: case 101: case 102: case 158: case 159:
			return 0;
		}
		if ( (32 <= wMID && wMID <= 58) || (85 <= wMID && wMID <= 96) || wMID == 156 || wMID == 157 )	return 1;
		if ( (59 <= wMID && wMID <= 62) || wMID == 21 )		return 2;
		switch ( wMID ) { case 4: case 5: case 6: case 8: case 22: case 111: return 3; }
		switch ( wMID ) { case 11: case 12: case 15: case 16: case 17: case 18: case 143: case 144: case 201: case 202: return 4; }
		switch ( wMID ) { case 71: case 72: case 73: case 80: case 152: case 153: case 154: case 155: return 5; }
		if ( 103 <= wMID && wMID <= 110 )	return 6;
		if ( (112 <= wMID && wMID <= 126) || wMID == 150 || wMID == 151 || wMID == 160 || (190 <= wMID && wMID <= 193) )	return 7;
		if ( wMID == 142 || wMID == 161 || wMID == 162 )	return 8;
		if ( 180 <= wMID && wMID < 200 )	return 9;
		return 10;
	}

	//	Placeholder names: empty, "Temp", or only digits, dots, brackets and spaces.
	bool IsJunk ( const char* szName )
	{
		if ( !szName || !szName[0] || 0 == strcmp ( szName, "Temp" ) )	return true;
		for ( const char* p = szName; *p; ++p )
			if ( !strchr ( ".[]0123456789 ", *p ) )	return false;
		return true;
	}

	//	In these categories SID 0 is a real item; in the others it is the category's header.
	bool IsRealSID0 ( WORD wMID )
	{
		return wMID == 4 || wMID == 5 || wMID == 6 || wMID == 8 || wMID == 13 || wMID == 14;
	}

	//	No wearing model in the game data: invisible in game.
	bool IsHidden ( WORD wMID, WORD wSID )
	{
		return wMID == 161 && 1 <= wSID && wSID <= 3;
	}

	CString Lower ( const char* szText )
	{
		CString str ( szText );
		str.MakeLower ();
		return str;
	}
}

CItemBankWindow::CItemBankWindow () :
	m_pTree ( NULL ),
	m_pTreeScroll ( NULL ),
	m_pGridScroll ( NULL ),
	m_pSearch ( NULL ),
	m_pAmount ( NULL ),
	m_pInfo ( NULL ),
	m_nClass ( 0 ),
	m_nCategory ( -1 ),
	m_nTopRow ( 0 ),
	m_bLoaded ( false )
{
	SecureZeroMemory ( m_pRow, sizeof(m_pRow) );
	SecureZeroMemory ( m_pClassButton, sizeof(m_pClassButton) );
}

CItemBankWindow::~CItemBankWindow ()
{
}

CUIControl* CItemBankWindow::CreateControl ( const char* szControl )
{
	CUIControl* pControl = new CUIControl;
	pControl->CreateSub ( this, szControl );
	RegisterControl ( pControl );
	return pControl;
}

void CItemBankWindow::CreateSubControl ()
{
	CD3DFontPar* pFont9 = DxFontMan::GetInstance().LoadDxFont ( _DEFAULT_FONT, 9, _DEFAULT_FONT_FLAG );

	CreateControl ( "ITEMBROWSER_MIDDLE_LINE" );
	CreateControl ( "ITEMBROWSER_TREE_SCROLLBAR_BACK" );
	CreateControl ( "ITEMBROWSER_GRID_SCROLLBAR_BACK" );

	for ( int i = 0; i < nCLASS; ++i )
	{
		CString strKeyword;
		strKeyword.Format ( "ITEMBROWSER_CLASS_BUTTON%d", i );
		CBasicTextButton* pButton = new CBasicTextButton;
		pButton->CreateSub ( this, "BASIC_TEXT_BUTTON14", UI_FLAG_XSIZE, ITEMBROWSER_CLASS0 + i );
		pButton->CreateBaseButton ( strKeyword.GetString(), CBasicTextButton::SIZE14, CBasicButton::RADIO_FLIP, szCLASS[i] );
		RegisterControl ( pButton );
		m_pClassButton[i] = pButton;
	}

	{
		CBasicTreeEx* pTree = new CBasicTreeEx;
		pTree->CreateSub ( this, "ITEMBROWSER_TREE", UI_FLAG_DEFAULT, ITEMBROWSER_TREE );
		pTree->SetFont ( pFont9 );
		pTree->SetUseLabelOverColor ( TRUE );
		pTree->SetLabelOverColor ( NS_UITEXTCOLOR::PRIVATE );
		RegisterControl ( pTree );
		m_pTree = pTree;
	}

	{
		CBasicLineBox* pLineBox = new CBasicLineBox;
		pLineBox->CreateSub ( this, "BASIC_LINE_BOX_EDIT", UI_FLAG_XSIZE | UI_FLAG_YSIZE );
		pLineBox->CreateBaseBoxEditBox ( "ITEMBROWSER_SEARCH_BACK" );
		RegisterControl ( pLineBox );

		CUIEditBox* pEditBox = new CUIEditBox;
		pEditBox->CreateSub ( this, "ITEMBROWSER_SEARCH", UI_FLAG_DEFAULT, ITEMBROWSER_SEARCH );
		pEditBox->CreateCarrat ( "ITEMBROWSER_CARRAT", TRUE, UINT_MAX );
		pEditBox->SetLimitInput ( 32 );
		pEditBox->SetFont ( pFont9 );
		RegisterControl ( pEditBox );
		m_pSearch = pEditBox;
	}

	for ( int i = 0; i < nROW; ++i )
	{
		CString strKeyword;
		strKeyword.Format ( "ITEMBROWSER_ITEM_SLOT%d", i );
		CItemSlot* pSlot = new CItemSlot;
		pSlot->CreateSub ( this, strKeyword.GetString(), UI_FLAG_DEFAULT, ITEMBROWSER_ROW0 + i );
		pSlot->CreateSubControl ( nCOLUMN, FALSE );
		RegisterControl ( pSlot );
		m_pRow[i] = pSlot;
	}

	{
		CBasicTextBox* pText = new CBasicTextBox;
		pText->CreateSub ( this, "ITEMBROWSER_AMOUNT_TEXT" );
		pText->SetFont ( pFont9 );
		pText->SetOneLineText ( "Amount" );
		RegisterControl ( pText );

		CBasicLineBox* pLineBox = new CBasicLineBox;
		pLineBox->CreateSub ( this, "BASIC_LINE_BOX_EDIT", UI_FLAG_XSIZE | UI_FLAG_YSIZE );
		pLineBox->CreateBaseBoxEditBox ( "ITEMBROWSER_AMOUNT_BACK" );
		RegisterControl ( pLineBox );

		CUIEditBox* pEditBox = new CUIEditBox;
		pEditBox->CreateSub ( this, "ITEMBROWSER_AMOUNT", UI_FLAG_DEFAULT, ITEMBROWSER_AMOUNT );
		pEditBox->CreateCarrat ( "ITEMBROWSER_CARRAT", TRUE, UINT_MAX );
		pEditBox->SetLimitInput ( 3 );
		pEditBox->DoUSE_NUMBER ( true );
		pEditBox->SetFont ( pFont9 );
		pEditBox->SetEditString ( CString("1") );
		RegisterControl ( pEditBox );
		m_pAmount = pEditBox;
	}

	{
		CBasicTextBox* pText = new CBasicTextBox;
		pText->CreateSub ( this, "ITEMBROWSER_INFO_TEXT" );
		pText->SetFont ( pFont9 );
		RegisterControl ( pText );
		m_pInfo = pText;
	}

	CBasicScrollBarEx* pScrollBar = new CBasicScrollBarEx;
	pScrollBar->CreateSub ( this, "BASIC_SCROLLBAR", UI_FLAG_YSIZE );
	pScrollBar->CreateBaseScrollBar ( "ITEMBROWSER_TREE_SCROLLBAR" );
	pScrollBar->GetThumbFrame()->SetState ( 1, 1 );
	RegisterControl ( pScrollBar );
	m_pTreeScroll = pScrollBar;

	pScrollBar = new CBasicScrollBarEx;
	pScrollBar->CreateSub ( this, "BASIC_SCROLLBAR", UI_FLAG_RIGHT | UI_FLAG_YSIZE );
	pScrollBar->CreateBaseScrollBar ( "ITEMBROWSER_GRID_SCROLLBAR" );
	pScrollBar->GetThumbFrame()->SetState ( 1, 1 );
	RegisterControl ( pScrollBar );
	m_pGridScroll = pScrollBar;
}

//	Every item in GLItemMan, by category (MID), and the tree of groups -> categories.
void CItemBankWindow::LoadCatalog ()
{
	if ( m_bLoaded )	return;
	m_bLoaded = true;

	GLItemMan& glItemMan = GLItemMan::GetInstance();
	for ( WORD wMID = 0; wMID < GLItemMan::MAX_MID; ++wMID )
	{
		SCATEGORY sCategory;
		sCategory.wMID = wMID;
		for ( WORD wSID = 0; wSID < GLItemMan::MAX_SID; ++wSID )
		{
			if ( wSID == 0 && !IsRealSID0 ( wMID ) )	continue;
			if ( IsHidden ( wMID, wSID ) )				continue;
			SITEM* pItem = glItemMan.GetItem ( wMID, wSID );
			if ( !pItem || IsJunk ( pItem->GetName() ) )	continue;
			sCategory.vecItems.push_back ( SNATIVEID ( wMID, wSID ) );
		}
		if ( sCategory.vecItems.empty () )	continue;

		const char* szName = CategoryName ( wMID );
		if ( !szName )
		{
			CString strKey;
			strKey.Format ( "IN_%03d_000", wMID );
			szName = GLStringTable::GetInstance().GetString ( strKey.GetString(), GLStringTable::ITEM );
			if ( !szName )
			{
				strKey.Format ( "IN_%d_0", wMID );
				szName = GLStringTable::GetInstance().GetString ( strKey.GetString(), GLStringTable::ITEM );
			}
		}
		if ( IsJunk ( szName ) || 0 == strncmp ( szName, "IN_", 3 ) || 0 == strcmp ( szName, "?" ) )
		{
			CString strName;
			strName.Format ( "Category %d", wMID );
			sCategory.strName = strName.GetString();
		}
		else
		{
			sCategory.strName = szName;
		}
		m_vecCategory.push_back ( sCategory );
	}

	const int nGROUP = sizeof(szGROUP) / sizeof(szGROUP[0]);
	for ( int g = 0; g < nGROUP; ++g )
	{
		CBasicTreeNode* pGroupNode = NULL;
		for ( size_t i = 0; i < m_vecCategory.size(); ++i )
		{
			if ( Group ( m_vecCategory[i].wMID ) != g )	continue;
			if ( !pGroupNode )
			{
				pGroupNode = m_pTree->InsertNode ( NULL, szGROUP[g] );
				pGroupNode->SetNodeData ( DWORD_PTR(0) );
				SGROUPNODE sGroup = { pGroupNode, g, -1 };
				m_vecGroupNode.push_back ( sGroup );
			}
			CBasicTreeNode* pNode = m_pTree->InsertNode ( pGroupNode, m_vecCategory[i].strName.c_str() );
			pNode->SetNodeData ( DWORD_PTR(i + 1) );
		}
	}
	UpdateGroupLabels ();
}

//	Groups read "[+] Weapons" closed and "[-] Weapons" open, in gold, so they look like what
//	they are: headers to click open (the tree's own expand arrow is a tiny dark glyph).
void CItemBankWindow::UpdateGroupLabels ()
{
	for ( size_t i = 0; i < m_vecGroupNode.size(); ++i )
	{
		SGROUPNODE& sGroup = m_vecGroupNode[i];
		const BOOL bOpen = sGroup.pNode->IsBranchVisible ();
		if ( bOpen == sGroup.bOpen )	continue;
		sGroup.bOpen = bOpen;

		CString strLabel;
		strLabel.Format ( "%s %s", bOpen ? "[-]" : "[+]", szGROUP[sGroup.nGroup] );
		sGroup.pNode->SetLabel ( strLabel, NS_UITEXTCOLOR::GOLD );
		sGroup.pNode->SetNodeData ( DWORD_PTR(0) );
	}
}

DWORD CItemBankWindow::ClassMask ( int nClass )
{
	static const DWORD dwBOTH[] = { GLCC_ALL_NEWSEX, GLCC_FIGHTER, GLCC_ARMS, GLCC_ARCHER, GLCC_SPIRIT, GLCC_EXTREME };
	if ( nClass <= 0 || nCLASS <= nClass )	return GLCC_ALL_NEWSEX;

	//	The character's own class: only what it can wear (its gender too).
	GLCharacter* pChar = GLGaeaClient::GetInstance().GetCharacter();
	if ( pChar && (pChar->m_emClass & dwBOTH[nClass]) )	return pChar->m_emClass;
	return dwBOTH[nClass];
}

void CItemBankWindow::SelectClass ( int nClass )
{
	m_nClass = nClass;
	for ( int i = 0; i < nCLASS; ++i )
		if ( m_pClassButton[i] )	m_pClassButton[i]->SetFlip ( i == nClass );
	Refilter ();
}

//	The items to show: the search (over every category) or the picked category, of the class.
void CItemBankWindow::Refilter ()
{
	m_vecShown.clear ();
	m_nTopRow = 0;

	const DWORD dwMask = ClassMask ( m_nClass );
	GLItemMan& glItemMan = GLItemMan::GetInstance();

	CString strSearch = m_strSearch;
	strSearch.Trim ();
	int nCodeMID = -1, nCodeSID = -1;
	const bool bCode = !strSearch.IsEmpty() && 2 == sscanf ( strSearch.GetString(), "%d %d", &nCodeMID, &nCodeSID );
	const CString strLower = Lower ( strSearch.GetString() );

	CString strTitle;
	for ( size_t c = 0; c < m_vecCategory.size(); ++c )
	{
		const SCATEGORY& sCategory = m_vecCategory[c];
		if ( strSearch.IsEmpty() && (int)c != m_nCategory )	continue;

		for ( size_t i = 0; i < sCategory.vecItems.size(); ++i )
		{
			const SNATIVEID sID = sCategory.vecItems[i];
			SITEM* pItem = glItemMan.GetItem ( sID );
			if ( !pItem )	continue;
			if ( m_nClass != 0 && !(pItem->sBasicOp.dwReqCharClass & dwMask) )	continue;

			if ( bCode )
			{
				if ( sID.wMainID != nCodeMID || sID.wSubID != nCodeSID )	continue;
			}
			else if ( !strSearch.IsEmpty() )
			{
				if ( Lower ( pItem->GetName() ).Find ( strLower ) < 0 )	continue;
			}
			m_vecShown.push_back ( sID );
		}
	}

	if ( m_pGridScroll )
	{
		const int nRows = (int(m_vecShown.size()) + nCOLUMN - 1) / nCOLUMN;
		m_pGridScroll->GetThumbFrame()->SetState ( nRows > 0 ? nRows : 1, nROW );
		m_pGridScroll->GetThumbFrame()->SetPercent ( 0.0f );
	}

	if ( m_pInfo )
	{
		CString strInfo;
		if ( !strSearch.IsEmpty() )
			strInfo.Format ( "\"%s\": %d items. Click one to get it.", strSearch.GetString(), int(m_vecShown.size()) );
		else if ( 0 <= m_nCategory && m_nCategory < (int)m_vecCategory.size() )
			strInfo.Format ( "%s: %d items. Click one to get it.", m_vecCategory[m_nCategory].strName.c_str(), int(m_vecShown.size()) );
		else
			strInfo = "Pick a category on the left, or search.";
		m_pInfo->SetOneLineText ( strInfo, NS_UITEXTCOLOR::DEFAULT );
	}

	ShowRows ();
}

void CItemBankWindow::ShowRows ()
{
	for ( int r = 0; r < nROW; ++r )
	{
		CItemSlot* pRow = m_pRow[r];
		if ( !pRow )	continue;
		for ( int c = 0; c < nCOLUMN; ++c )
		{
			const size_t nIndex = size_t(m_nTopRow + r) * nCOLUMN + c;
			if ( nIndex < m_vecShown.size() )
			{
				SINVENITEM sInvenItem;
				sInvenItem.sItemCustom.sNativeID = m_vecShown[nIndex];
				sInvenItem.wPosX = WORD(c);
				sInvenItem.wPosY = WORD(r);
				pRow->SetItemImage ( c, sInvenItem );
			}
			else
			{
				pRow->ResetItemImage ( c );
			}
		}
	}
}

WORD CItemBankWindow::Amount ()
{
	int nAmount = m_pAmount ? atoi ( m_pAmount->GetEditString().GetString() ) : 1;
	if ( nAmount < 1 )				nAmount = 1;
	if ( nAmount > nMAX_COUNT )		nAmount = nMAX_COUNT;
	return WORD(nAmount);
}

void CItemBankWindow::EndEdits ()
{
	if ( m_pSearch )	m_pSearch->EndEdit ();
	if ( m_pAmount )	m_pAmount->EndEdit ();
}

void CItemBankWindow::InitItemBank ()
{
	LoadCatalog ();

	//	Start on the character's own class.
	int nClass = 0;
	GLCharacter* pChar = GLGaeaClient::GetInstance().GetCharacter();
	if ( pChar )
	{
		const DWORD dwClass = pChar->m_emClass;
		if ( dwClass & GLCC_FIGHTER )		nClass = 1;
		else if ( dwClass & GLCC_ARMS )		nClass = 2;
		else if ( dwClass & GLCC_ARCHER )	nClass = 3;
		else if ( dwClass & GLCC_SPIRIT )	nClass = 4;
		else if ( dwClass & GLCC_EXTREME )	nClass = 5;
	}
	SelectClass ( nClass );
}

void CItemBankWindow::ClearItemBank ()
{
}

void CItemBankWindow::SetVisibleSingle ( BOOL bVisible )
{
	CUIWindowEx::SetVisibleSingle ( bVisible );
	if ( !bVisible )	EndEdits ();
}

void CItemBankWindow::Update ( int x, int y, BYTE LB, BYTE MB, BYTE RB, int nScroll, float fElapsedTime, BOOL bFirstControl )
{
	CUIWindowEx::Update ( x, y, LB, MB, RB, nScroll, fElapsedTime, bFirstControl );

	if ( m_pSearch && m_pSearch->GetEditString() != m_strSearch )
	{
		m_strSearch = m_pSearch->GetEditString();
		Refilter ();
	}

	if ( m_pTreeScroll && m_pTree )
	{
		CBasicScrollThumbFrame* const pThumbFrame = m_pTreeScroll->GetThumbFrame ();
		m_pTree->SetCurLine ( 0 );
		const int nTotalLine = m_pTree->GetTotalLine ();
		const int nLinePerOneView = m_pTree->GetLinePerOneView ();
		pThumbFrame->SetState ( nTotalLine ? nTotalLine : 1, nLinePerOneView ? nLinePerOneView : 1 );
		if ( nLinePerOneView < nTotalLine )
		{
			int nPos = (int)floor ( pThumbFrame->GetPercent () * (nTotalLine - nLinePerOneView) );
			m_pTree->SetCurLine ( nPos < 0 ? 0 : nPos );
		}
	}

	if ( m_pGridScroll )
	{
		const int nRows = (int(m_vecShown.size()) + nCOLUMN - 1) / nCOLUMN;
		int nTop = 0;
		if ( nROW < nRows )
		{
			nTop = (int)floor ( m_pGridScroll->GetThumbFrame()->GetPercent () * (nRows - nROW) );
			if ( nTop < 0 )	nTop = 0;
		}
		if ( nTop != m_nTopRow )
		{
			m_nTopRow = nTop;
			ShowRows ();
		}
	}
}

void CItemBankWindow::TranslateUIMessage ( UIGUID ControlID, DWORD dwMsg )
{
	CUIWindowEx::TranslateUIMessage ( ControlID, dwMsg );

	if ( ET_CONTROL_TITLE == ControlID || ET_CONTROL_TITLE_F == ControlID )
	{
		if ( (dwMsg & UIMSG_LB_DUP) && CHECK_MOUSE_IN ( dwMsg ) )
			CInnerInterface::GetInstance().SetDefaultPosInterface( ITEMBANK_WINDOW );
		return;
	}

	if ( ITEMBROWSER_CLASS0 <= ControlID && ControlID < ITEMBROWSER_CLASS0 + nCLASS )
	{
		if ( CHECK_MOUSEIN_LBUPLIKE ( dwMsg ) )	SelectClass ( ControlID - ITEMBROWSER_CLASS0 );
		return;
	}

	switch ( ControlID )
	{
	case ITEMBROWSER_TREE:
		if ( CHECK_MOUSEIN_LBUPLIKE ( dwMsg ) && m_pTree )
		{
			CBasicTreeNode* pNode = m_pTree->GetSelectedNode ();
			const int nData = pNode ? int(pNode->GetNodeData ()) : 0;
			if ( nData > 0 )
			{
				m_nCategory = nData - 1;
				if ( m_pSearch )	m_pSearch->ClearEdit ();
				m_strSearch.Empty ();
				Refilter ();
			}
			else if ( pNode && (dwMsg & UIMSG_LB_UP) && !(dwMsg & UIMSG_LB_DUP) )
			{
				//	A group: one click opens or closes it (the tree alone wants a double click).
				pNode->ToggleBranch ();
			}
			UpdateGroupLabels ();
		}
		return;

	case ITEMBROWSER_SEARCH:
	case ITEMBROWSER_AMOUNT:
		{
			CUIEditBox* pEdit = ControlID == ITEMBROWSER_SEARCH ? m_pSearch : m_pAmount;
			CUIEditBox* pOther = ControlID == ITEMBROWSER_SEARCH ? m_pAmount : m_pSearch;
			if ( CHECK_MOUSE_IN ( dwMsg ) )
			{
				if ( (UIMSG_LB_UP & dwMsg) && pEdit && !pEdit->IsBegin () )
				{
					if ( pOther )	pOther->EndEdit ();
					pEdit->BeginEdit ();
				}
			}
			else if ( (UIMSG_LB_DOWN & dwMsg) || (UIMSG_LB_UP & dwMsg) )
			{
				if ( pEdit )	pEdit->EndEdit ();
			}
		}
		return;
	}

	if ( ITEMBROWSER_ROW0 <= ControlID && ControlID < ITEMBROWSER_ROW_END )
	{
		if ( !CHECK_MOUSE_IN ( dwMsg ) )	return;

		CItemSlot* pRow = m_pRow[ControlID - ITEMBROWSER_ROW0];
		const int nColumn = pRow ? pRow->GetItemIndex () : -1;
		if ( nColumn < 0 )	return;

		SINVENITEM& sInvenItem = pRow->GetItemImage ( nColumn );
		if ( sInvenItem.sItemCustom.sNativeID == NATIVEID_NULL () )	return;

		SITEMCUSTOM sItemCustom = sInvenItem.sItemCustom;
		CInnerInterface::GetInstance().SHOW_ITEM_INFO ( sItemCustom, FALSE, FALSE, FALSE, 0, 0 );

		if ( dwMsg & UIMSG_LB_UP )
		{
			EndEdits ();
			GLGaeaClient::GetInstance().GetCharacter()->ReqItemBrowserGet ( sItemCustom.sNativeID, Amount () );
		}
	}
}
