#pragma	once

#include "UIWindowEx.h"
#include "GLDefine.h"
#include <string>
#include <vector>

class	CItemSlot;
class	CBasicTextBox;
class	CBasicTextButton;
class	CBasicTreeEx;
class	CBasicScrollBarEx;
class	CUIEditBox;

//	The B window: the item browser. Every item in the game, by category (the tree on the left,
//	like the Help window) and by class (the buttons on top), with a search box; clicking an item
//	asks the server to put it in the inventory (GLCharacter::ReqItemBrowserGet). Its layout is
//	data/gui/itembrowser.xml. (It used to be the item bank of cash-shop purchases.)
class	CItemBankWindow : public CUIWindowEx
{
protected:
	enum
	{
		nCOLUMN = 7,
		nROW = 9,
		nCLASS = 6,
		nMAX_COUNT = 999,
	};

	enum
	{
		ITEMBROWSER_TREE = ET_CONTROL_NEXT,
		ITEMBROWSER_SEARCH,
		ITEMBROWSER_AMOUNT,
		ITEMBROWSER_CLASS0,
		ITEMBROWSER_ROW0 = ITEMBROWSER_CLASS0 + nCLASS,
		ITEMBROWSER_ROW_END = ITEMBROWSER_ROW0 + nROW,
	};

	struct SCATEGORY
	{
		WORD					wMID;
		std::string				strName;
		std::vector<SNATIVEID>	vecItems;
	};

public:
	CItemBankWindow ();
	virtual	~CItemBankWindow ();

public:
	void	CreateSubControl ();

public:
	virtual	void Update ( int x, int y, BYTE LB, BYTE MB, BYTE RB, int nScroll, float fElapsedTime, BOOL bFirstControl );
	virtual	void TranslateUIMessage ( UIGUID ControlID, DWORD dwMsg );
	virtual	void SetVisibleSingle ( BOOL bVisible );

public:
	void	InitItemBank ();
	void	ClearItemBank();

private:
	void	LoadCatalog ();
	void	SelectClass ( int nClass );
	void	Refilter ();
	void	ShowRows ();
	DWORD	ClassMask ( int nClass );
	WORD	Amount ();
	void	EndEdits ();

	CUIControl*	CreateControl ( const char* szControl );

private:
	CBasicTreeEx*		m_pTree;
	CBasicScrollBarEx*	m_pTreeScroll;
	CBasicScrollBarEx*	m_pGridScroll;
	CItemSlot*			m_pRow[nROW];
	CBasicTextButton*	m_pClassButton[nCLASS];
	CUIEditBox*			m_pSearch;
	CUIEditBox*			m_pAmount;
	CBasicTextBox*		m_pInfo;

	std::vector<SCATEGORY>	m_vecCategory;
	std::vector<SNATIVEID>	m_vecShown;

	int			m_nClass;			//	0 = all classes, 1..5 = brawler, swordsman, archer, shaman, extreme
	int			m_nCategory;		//	index into m_vecCategory, -1 = none picked yet
	int			m_nTopRow;
	CString		m_strSearch;
	bool		m_bLoaded;
};
