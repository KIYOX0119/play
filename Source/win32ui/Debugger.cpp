#include "../iop/IopBios.h"
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <iostream>
#include "../AppConfig.h"
#include "../MIPSAssembler.h"
#include "../Ps2Const.h"
#include "../PS2OS.h"
#include "../MipsFunctionPatternDb.h"
#include "StdStream.h"
#include "win32/AcceleratorTableGenerator.h"
#include "win32/InputBox.h"
#include "xml/Parser.h"
#include "Debugger.h"
#include "resource.h"
#include "PtrMacro.h"
#include "string_cast.h"

#define CLSNAME			_T("CDebugger")

#define ID_EDIT_COPY	(0xE001)
#define WM_EXECUNLOAD	(WM_USER + 0)
#define WM_EXECCHANGE	(WM_USER + 1)

CDebugger::CDebugger(CPS2VM& virtualMachine)
: m_virtualMachine(virtualMachine)
{
	RECT rc;

	RegisterPreferences();

	if(!DoesWindowClassExist(CLSNAME))
	{
		WNDCLASSEX wc;
		memset(&wc, 0, sizeof(WNDCLASSEX));
		wc.cbSize			= sizeof(WNDCLASSEX);
		wc.hCursor			= LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground	= (HBRUSH)GetStockObject(GRAY_BRUSH); 
		wc.hInstance		= GetModuleHandle(NULL);
		wc.lpszClassName	= CLSNAME;
		wc.lpfnWndProc		= CWindow::WndProc;
		RegisterClassEx(&wc);
	}
	
	SetRect(&rc, 0, 0, 640, 480);

	Create(NULL, CLSNAME, _T(""), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, &rc, NULL, NULL);
	SetClassPtr();

	SetMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_DEBUGGER)));

	CreateClient(NULL);

	//Show(SW_MAXIMIZE);

	SetRect(&rc, 0, 0, 320, 240);

	//ELF View Initialization
	m_pELFView = new CELFView(m_pMDIClient->m_hWnd);
	m_pELFView->Show(SW_HIDE);

	//Functions View Initialization
	m_pFunctionsView = new CFunctionsView(m_pMDIClient->m_hWnd);
	m_pFunctionsView->Show(SW_HIDE);
	m_pFunctionsView->OnFunctionDblClick.connect(boost::bind(&CDebugger::OnFunctionsViewFunctionDblClick, this, _1));
	m_pFunctionsView->OnFunctionsStateChange.connect(boost::bind(&CDebugger::OnFunctionsViewFunctionsStateChange, this));

	//Threads View Initialization
	m_threadsView = new CThreadsViewWnd(m_pMDIClient->m_hWnd, m_virtualMachine);
	m_threadsView->Show(SW_HIDE);
	m_threadsView->OnGotoAddress.connect(boost::bind(&CDebugger::OnThreadsViewAddressDblClick, this, _1));

	//Debug Views Initialization
	m_nCurrentView = -1;

	memset(m_pView, 0, sizeof(m_pView));
	m_pView[DEBUGVIEW_EE]	= new CDebugView(m_pMDIClient->m_hWnd, m_virtualMachine, &m_virtualMachine.m_EE, 
		std::bind(&CPS2VM::StepEe, &m_virtualMachine), m_virtualMachine.m_os, "EmotionEngine");
	m_pView[DEBUGVIEW_VU0]	= new CDebugView(m_pMDIClient->m_hWnd, m_virtualMachine, &m_virtualMachine.m_VU0, 
		std::bind(&CPS2VM::StepEe, &m_virtualMachine), nullptr, "Vector Unit 0");
	m_pView[DEBUGVIEW_VU1]	= new CDebugView(m_pMDIClient->m_hWnd, m_virtualMachine, &m_virtualMachine.m_VU1, 
		std::bind(&CPS2VM::StepVu1, &m_virtualMachine), nullptr, "Vector Unit 1");
	m_pView[DEBUGVIEW_IOP]  = new CDebugView(m_pMDIClient->m_hWnd, m_virtualMachine, &m_virtualMachine.m_iop.m_cpu, 
		std::bind(&CPS2VM::StepIop, &m_virtualMachine), m_virtualMachine.m_iopOs, "IO Processor");

	m_virtualMachine.m_os->OnExecutableChange.connect(boost::bind(&CDebugger::OnExecutableChange, this));
	m_virtualMachine.m_os->OnExecutableUnloading.connect(boost::bind(&CDebugger::OnExecutableUnloading, this));

	ActivateView(DEBUGVIEW_EE);
	LoadSettings();

	if(GetDisassemblyWindow()->IsVisible())
	{
		GetDisassemblyWindow()->SetFocus();
	}

	UpdateLoggingMenu();
	CreateAccelerators();
}

CDebugger::~CDebugger()
{
	OnExecutableUnloadingMsg();

	DestroyAccelerators();

	SaveSettings();

	for(unsigned int i = 0; i < DEBUGVIEW_MAX; i++)
	{
		delete m_pView[i];
	}

	DELETEPTR(m_pELFView);
	DELETEPTR(m_pFunctionsView);
}

HACCEL CDebugger::GetAccelerators()
{
	return m_nAccTable;
}

void CDebugger::RegisterPreferences()
{
	CAppConfig& config(CAppConfig::GetInstance());

	config.RegisterPreferenceInteger("debugger.log.posx",				0);
	config.RegisterPreferenceInteger("debugger.log.posy",				0);
	config.RegisterPreferenceInteger("debugger.log.sizex",				0);
	config.RegisterPreferenceInteger("debugger.log.sizey",				0);
	config.RegisterPreferenceBoolean("debugger.log.visible",			true);

	config.RegisterPreferenceInteger("debugger.disasm.posx",			0);
	config.RegisterPreferenceInteger("debugger.disasm.posy",			0);
	config.RegisterPreferenceInteger("debugger.disasm.sizex",			0);
	config.RegisterPreferenceInteger("debugger.disasm.sizey",			0);
	config.RegisterPreferenceBoolean("debugger.disasm.visible",			true);

	config.RegisterPreferenceInteger("debugger.regview.posx",			0);
	config.RegisterPreferenceInteger("debugger.regview.posy",			0);
	config.RegisterPreferenceInteger("debugger.regview.sizex",			0);
	config.RegisterPreferenceInteger("debugger.regview.sizey",			0);
	config.RegisterPreferenceBoolean("debugger.regview.visible",		true);

	config.RegisterPreferenceInteger("debugger.memoryview.posx",		0);
	config.RegisterPreferenceInteger("debugger.memoryview.posy",		0);
	config.RegisterPreferenceInteger("debugger.memoryview.sizex",		0);
	config.RegisterPreferenceInteger("debugger.memoryview.sizey",		0);
	config.RegisterPreferenceBoolean("debugger.memoryview.visible",		true);

	config.RegisterPreferenceInteger("debugger.callstack.posx",			0);
	config.RegisterPreferenceInteger("debugger.callstack.posy",			0);
	config.RegisterPreferenceInteger("debugger.callstack.sizex",		0);
	config.RegisterPreferenceInteger("debugger.callstack.sizey",		0);
	config.RegisterPreferenceBoolean("debugger.callstack.visible",		true);
}

void CDebugger::UpdateLoggingMenu()
{
	HMENU hMenu = GetMenu(m_hWnd);

	hMenu = GetSubMenu(hMenu, 2);

	const int stateCount = 8;
	bool nState[stateCount];
	memset(nState, 0, sizeof(nState));
//	nState[0] = m_virtualMachine.m_Logging.GetGSLoggingStatus();
//	nState[1] = m_virtualMachine.m_Logging.GetDMACLoggingStatus();
//	nState[2] = m_virtualMachine.m_Logging.GetIPULoggingStatus();
//	nState[3] = m_virtualMachine.m_Logging.GetOSLoggingStatus();
//	nState[4] = m_virtualMachine.m_Logging.GetOSRecordingStatus();
//	nState[5] = m_virtualMachine.m_Logging.GetSIFLoggingStatus();
//	nState[6] = m_virtualMachine.m_Logging.GetIOPLoggingStatus();
#ifdef DEBUGGER_INCLUDED
	nState[7] = m_virtualMachine.IsSaveVpuStateEnabled();
#endif
	for(unsigned int i = 0; i < stateCount; i++)
	{
		MENUITEMINFO mii;

		memset(&mii, 0, sizeof(MENUITEMINFO));
		mii.cbSize		= sizeof(MENUITEMINFO);
		mii.fMask		= MIIM_STATE;
		mii.fState		= nState[i] ? MFS_CHECKED : 0;

		SetMenuItemInfo(hMenu, i, TRUE, &mii);
	}
}

void CDebugger::UpdateTitle()
{
	std::tstring sTitle(_T("Play! - Debugger"));

	if(GetCurrentView() != NULL)
	{
		sTitle += 
			_T(" - [ ") + 
			string_cast<std::tstring>(GetCurrentView()->GetName()) +
			_T(" ]");
	}

	SetText(sTitle.c_str());
}

void CDebugger::LoadSettings()
{
	LoadViewLayout();
}

void CDebugger::SaveSettings()
{
	SaveViewLayout();
}

void CDebugger::SerializeWindowGeometry(CWindow* pWindow, const char* sPosX, const char* sPosY, const char* sSizeX, const char* sSizeY, const char* sVisible)
{
	RECT rc;
	CAppConfig& config(CAppConfig::GetInstance());

	pWindow->GetWindowRect(&rc);
	ScreenToClient(m_pMDIClient->m_hWnd, (POINT*)&rc + 0);
	ScreenToClient(m_pMDIClient->m_hWnd, (POINT*)&rc + 1);

	config.SetPreferenceInteger(sPosX, rc.left);
	config.SetPreferenceInteger(sPosY, rc.top);

	if(sSizeX != NULL && sSizeY != NULL)
	{
		config.SetPreferenceInteger(sSizeX, (rc.right - rc.left));
		config.SetPreferenceInteger(sSizeY, (rc.bottom - rc.top));
	}

	config.SetPreferenceBoolean(sVisible, pWindow->IsVisible());
}

void CDebugger::UnserializeWindowGeometry(CWindow* pWindow, const char* sPosX, const char* sPosY, const char* sSizeX, const char* sSizeY, const char* sVisible)
{
	CAppConfig& config(CAppConfig::GetInstance());

	pWindow->SetPosition(config.GetPreferenceInteger(sPosX), config.GetPreferenceInteger(sPosY));
	pWindow->SetSize(config.GetPreferenceInteger(sSizeX), config.GetPreferenceInteger(sSizeY));

	if(!config.GetPreferenceBoolean(sVisible))
	{
		pWindow->Show(SW_HIDE);
	}
	else
	{
		pWindow->Show(SW_SHOW);
	}
}

void CDebugger::Resume()
{
	m_virtualMachine.Resume();
}

void CDebugger::StepCPU()
{
	if(m_virtualMachine.GetStatus() == CVirtualMachine::RUNNING)
	{
		MessageBeep(-1);
		return;
	}
	
	if(::GetParent(GetFocus()) != GetDisassemblyWindow()->m_hWnd)
	{
		GetDisassemblyWindow()->SetFocus();
	}

	GetCurrentView()->Step();
}

void CDebugger::FindValue()
{
	Framework::Win32::CInputBox Input(_T("Find Value in Memory"), _T("Enter value to find:"), _T("00000000"));
	
	const TCHAR* sValue = Input.GetValue(m_hWnd);
	if(sValue == NULL) return;

	uint32 nValue = 0;
	_stscanf(sValue, _T("%x"), &nValue);
	if(nValue == 0) return;

	printf("Search results for 0x%0.8X\r\n", nValue);
	printf("-----------------------------\r\n");
	for(unsigned int i = 0; i < PS2::EERAMSIZE; i += 4)
	{
		if(*(uint32*)&m_virtualMachine.m_ram[i] == nValue)
		{
			printf("0x%0.8X\r\n", i);
		}
	}
}

void CDebugger::AssembleJAL()
{
	Framework::Win32::CInputBox InputTarget(_T("Assemble JAL"), _T("Enter jump target:"), _T("00000000"));
	Framework::Win32::CInputBox InputAssemble(_T("Assemble JAL"), _T("Enter address to assemble JAL to:"), _T("00000000"));

	const TCHAR* sTarget = InputTarget.GetValue(m_hWnd);
	if(sTarget == NULL) return;

	const TCHAR* sAssemble = InputAssemble.GetValue(m_hWnd);
	if(sAssemble == NULL) return;

	uint32 nValueTarget = 0, nValueAssemble = 0;
	_stscanf(sTarget, _T("%x"), &nValueTarget);
	_stscanf(sAssemble, _T("%x"), &nValueAssemble);

	*(uint32*)&m_virtualMachine.m_ram[nValueAssemble] = 0x0C000000 | (nValueTarget / 4);
}

void CDebugger::ReanalyzeEe()
{
	std::pair<uint32, uint32> executableRange = m_virtualMachine.m_os->GetExecutableRange();
	uint32 nMinAddr = executableRange.first;
	uint32 nMaxAddr = executableRange.second & ~0x03;

	m_virtualMachine.m_EE.m_pAnalysis->Clear();
	m_virtualMachine.m_EE.m_pAnalysis->Analyse(nMinAddr, nMaxAddr);
}

void CDebugger::FindEeFunctions()
{
	std::pair<uint32, uint32> executableRange = m_virtualMachine.m_os->GetExecutableRange();
	uint32 nMinAddr = executableRange.first;
	uint32 nMaxAddr = executableRange.second & ~0x03;

	{
		Framework::CStdStream patternStream("ee_functions.xml", "rb");
		boost::scoped_ptr<Framework::Xml::CNode> document(Framework::Xml::CParser::ParseDocument(patternStream));
		CMipsFunctionPatternDb patternDb(document.get());

		for(auto patternIterator(std::begin(patternDb.GetPatterns()));
			patternIterator != std::end(patternDb.GetPatterns()); ++patternIterator)
		{
			auto pattern = *patternIterator;
			for(uint32 address = nMinAddr; address <= nMaxAddr; address += 4)
			{
				uint32* text = reinterpret_cast<uint32*>(m_virtualMachine.m_ram + address);
				uint32 textSize = (nMaxAddr - address);
				if(pattern.Matches(text, textSize))
				{
					m_virtualMachine.m_EE.m_Functions.InsertTag(address, pattern.name.c_str());
					break;
				}
			}
		}

		m_virtualMachine.m_EE.m_Functions.OnTagListChange();
	}
}

void CDebugger::Layout1024()
{
	GetDisassemblyWindow()->SetPosition(0, 0);
	GetDisassemblyWindow()->SetSize(700, 435);
	GetDisassemblyWindow()->Show(SW_SHOW);

	GetRegisterViewWindow()->SetPosition(700, 0);
	GetRegisterViewWindow()->SetSize(324, 572);
	GetRegisterViewWindow()->Show(SW_SHOW);

	GetMemoryViewWindow()->SetPosition(0, 435);
	GetMemoryViewWindow()->SetSize(700, 265);
	GetMemoryViewWindow()->Show(SW_SHOW);

	GetCallStackWindow()->SetPosition(700, 572);
	GetCallStackWindow()->SetSize(324, 128);
	GetCallStackWindow()->Show(SW_SHOW);
}

void CDebugger::Layout1280()
{
	GetDisassemblyWindow()->SetPosition(0, 0);
	GetDisassemblyWindow()->SetSize(900, 540);
	GetDisassemblyWindow()->Show(SW_SHOW);

	GetRegisterViewWindow()->SetPosition(900, 0);
	GetRegisterViewWindow()->SetSize(380, 784);
	GetRegisterViewWindow()->Show(SW_SHOW);

	GetMemoryViewWindow()->SetPosition(0, 540);
	GetMemoryViewWindow()->SetSize(900, 416);
	GetMemoryViewWindow()->Show(SW_SHOW);

	GetCallStackWindow()->SetPosition(900, 784);
	GetCallStackWindow()->SetSize(380, 172);
	GetCallStackWindow()->Show(SW_SHOW);
}

void CDebugger::Layout1600()
{
	GetDisassemblyWindow()->SetPosition(0, 0);
	GetDisassemblyWindow()->SetSize(1094, 725);
	GetDisassemblyWindow()->Show(SW_SHOW);

	GetRegisterViewWindow()->SetPosition(1094, 0);
	GetRegisterViewWindow()->SetSize(506, 725);
	GetRegisterViewWindow()->Show(SW_SHOW);

	GetMemoryViewWindow()->SetPosition(0, 725);
	GetMemoryViewWindow()->SetSize(1094, 407);
	GetMemoryViewWindow()->Show(SW_SHOW);

	GetCallStackWindow()->SetPosition(1094, 725);
	GetCallStackWindow()->SetSize(506, 407);
	GetCallStackWindow()->Show(SW_SHOW);
}

void CDebugger::InitializeConsole()
{
	AllocConsole();

	CONSOLE_SCREEN_BUFFER_INFO ScreenBufferInfo;

	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &ScreenBufferInfo);
	ScreenBufferInfo.dwSize.Y = 1000;
	SetConsoleScreenBufferSize(GetStdHandle(STD_OUTPUT_HANDLE), ScreenBufferInfo.dwSize);

	(*stdout) = *_fdopen(_open_osfhandle(
		reinterpret_cast<intptr_t>(GetStdHandle(STD_OUTPUT_HANDLE)),
		_O_TEXT), "w");

	setvbuf(stdout, NULL, _IONBF, 0);
	std::ios::sync_with_stdio();
}

void CDebugger::ActivateView(unsigned int nView)
{
	if(m_nCurrentView == nView) return;

	if(m_nCurrentView != -1)
	{
		SaveViewLayout();
		GetCurrentView()->Hide();
	}

	m_nCurrentView = nView;
	LoadViewLayout();
	UpdateTitle();
	{
		auto biosDebugInfoProvider = GetCurrentView()->GetBiosDebugInfoProvider();
		m_pFunctionsView->SetContext(GetCurrentView()->GetContext(), biosDebugInfoProvider);
		m_threadsView->SetContext(GetCurrentView()->GetContext(), biosDebugInfoProvider);
	}

	if(GetDisassemblyWindow()->IsVisible())
	{
		GetDisassemblyWindow()->SetFocus();
	}
}

void CDebugger::SaveViewLayout()
{
	SerializeWindowGeometry(GetDisassemblyWindow(), \
		"debugger.disasm.posx", \
		"debugger.disasm.posy", \
		"debugger.disasm.sizex", \
		"debugger.disasm.sizey", \
		"debugger.disasm.visible");

	SerializeWindowGeometry(GetRegisterViewWindow(), \
		"debugger.regview.posx", \
		"debugger.regview.posy", \
		"debugger.regview.sizex", \
		"debugger.regview.sizey", \
		"debugger.regview.visible");

	SerializeWindowGeometry(GetMemoryViewWindow(), \
		"debugger.memoryview.posx", \
		"debugger.memoryview.posy", \
		"debugger.memoryview.sizex", \
		"debugger.memoryview.sizey", \
		"debugger.memoryview.visible");

	SerializeWindowGeometry(GetCallStackWindow(), \
		"debugger.callstack.posx", \
		"debugger.callstack.posy", \
		"debugger.callstack.sizex", \
		"debugger.callstack.sizey", \
		"debugger.callstack.visible");
}

void CDebugger::LoadViewLayout()
{
	UnserializeWindowGeometry(GetDisassemblyWindow(), \
		"debugger.disasm.posx", \
		"debugger.disasm.posy", \
		"debugger.disasm.sizex", \
		"debugger.disasm.sizey", \
		"debugger.disasm.visible");

	UnserializeWindowGeometry(GetRegisterViewWindow(), \
		"debugger.regview.posx", \
		"debugger.regview.posy", \
		"debugger.regview.sizex", \
		"debugger.regview.sizey", \
		"debugger.regview.visible");

	UnserializeWindowGeometry(GetMemoryViewWindow(), \
		"debugger.memoryview.posx", \
		"debugger.memoryview.posy", \
		"debugger.memoryview.sizex", \
		"debugger.memoryview.sizey", \
		"debugger.memoryview.visible");

	UnserializeWindowGeometry(GetCallStackWindow(), \
		"debugger.callstack.posx", \
		"debugger.callstack.posy", \
		"debugger.callstack.sizex", \
		"debugger.callstack.sizey", \
		"debugger.callstack.visible");
}

CDebugView* CDebugger::GetCurrentView()
{
	if(m_nCurrentView == -1) return NULL;
	return m_pView[m_nCurrentView];
}

CMIPS* CDebugger::GetContext()
{
	return GetCurrentView()->GetContext();
}

CDisAsmWnd* CDebugger::GetDisassemblyWindow()
{
	return GetCurrentView()->GetDisassemblyWindow();
}

CMemoryViewMIPSWnd* CDebugger::GetMemoryViewWindow()
{
	return GetCurrentView()->GetMemoryViewWindow();
}

CRegViewWnd* CDebugger::GetRegisterViewWindow()
{
	return GetCurrentView()->GetRegisterViewWindow();
}

CCallStackWnd* CDebugger::GetCallStackWindow()
{
	return GetCurrentView()->GetCallStackWindow();
}

void CDebugger::CreateAccelerators()
{
	Framework::Win32::CAcceleratorTableGenerator generator;
	generator.Insert(ID_VM_SAVESTATE,			VK_F7,	FVIRTKEY);
	generator.Insert(ID_VM_LOADSTATE,			VK_F8,	FVIRTKEY);
	generator.Insert(ID_VIEW_FUNCTIONS,			'F',	FCONTROL | FVIRTKEY);
	generator.Insert(ID_VIEW_THREADS,			'T',	FCONTROL | FVIRTKEY);
	generator.Insert(ID_VM_STEP,				VK_F10,	FVIRTKEY);
	generator.Insert(ID_VM_RESUME,				VK_F5,	FVIRTKEY);
	generator.Insert(ID_VIEW_CALLSTACK,			'A',	FCONTROL | FVIRTKEY);
	generator.Insert(ID_VIEW_EEVIEW,			'1',	FALT | FVIRTKEY);
	generator.Insert(ID_VIEW_VU0VIEW,			'2',	FALT | FVIRTKEY);
	generator.Insert(ID_VIEW_VU1VIEW,			'3',	FALT | FVIRTKEY);
	generator.Insert(ID_VIEW_IOPVIEW,			'4',	FALT | FVIRTKEY);
	generator.Insert(ID_EDIT_COPY,				'C',	FCONTROL | FVIRTKEY);
	m_nAccTable = generator.Create();
}

void CDebugger::DestroyAccelerators()
{
	DestroyAcceleratorTable(m_nAccTable);
}

long CDebugger::OnCommand(unsigned short nID, unsigned short nMsg, HWND hFrom)
{
	switch(nID)
	{
	case ID_VM_STEP:
		StepCPU();
		break;
	case ID_VM_RESUME:
		Resume();
		break;
	case ID_VM_SAVESTATE:
		m_virtualMachine.SaveState("./config/state.sta");
		break;
	case ID_VM_LOADSTATE:
		m_virtualMachine.LoadState("./config/state.sta");
		break;
	case ID_VM_DUMPINTCHANDLERS:
		m_virtualMachine.DumpEEIntcHandlers();
		break;
	case ID_VM_DUMPDMACHANDLERS:
		m_virtualMachine.DumpEEDmacHandlers();
		break;
	case ID_VM_ASMJAL:
		AssembleJAL();
		break;
	case ID_VM_REANALYZE_EE:
		ReanalyzeEe();
		break;
	case ID_VM_FINDEEFUNCTIONS:
		FindEeFunctions();
		break;
	case ID_VM_FINDVALUE:
		FindValue();
		break;
	case ID_VIEW_MEMORY:
		GetMemoryViewWindow()->Show(SW_SHOW);
		GetMemoryViewWindow()->SetFocus();
		return FALSE;
		break;
	case ID_VIEW_CALLSTACK:
		GetCallStackWindow()->Show(SW_SHOW);
		GetCallStackWindow()->SetFocus();
		return FALSE;
		break;
	case ID_VIEW_FUNCTIONS:
		m_pFunctionsView->Show(SW_SHOW);
		m_pFunctionsView->SetFocus();
		return FALSE;
		break;
	case ID_VIEW_ELF:
		m_pELFView->Show(SW_SHOW);
		m_pELFView->SetFocus();
		return FALSE;
		break;
	case ID_VIEW_THREADS:
		m_threadsView->Show(SW_SHOW);
		m_threadsView->SetFocus();
		return FALSE;
		break;
	case ID_VIEW_DISASSEMBLY:
		GetDisassemblyWindow()->Show(SW_SHOW);
		GetDisassemblyWindow()->SetFocus();
		return FALSE;
		break;
	case ID_VIEW_EEVIEW:
		ActivateView(DEBUGVIEW_EE);
		break;
	case ID_VIEW_VU0VIEW:
		ActivateView(DEBUGVIEW_VU0);
		break;
	case ID_VIEW_VU1VIEW:
		ActivateView(DEBUGVIEW_VU1);
		break;
	case ID_VIEW_IOPVIEW:
		ActivateView(DEBUGVIEW_IOP);
		break;
	case ID_LOGGING_GS:
//		m_virtualMachine.m_Logging.SetGSLoggingStatus(!m_virtualMachine.m_Logging.GetGSLoggingStatus());
		UpdateLoggingMenu();
		break;
	case ID_LOGGING_DMAC:
//		m_virtualMachine.m_Logging.SetDMACLoggingStatus(!m_virtualMachine.m_Logging.GetDMACLoggingStatus());
		UpdateLoggingMenu();
		break;
	case ID_LOGGING_IPU:
//		m_virtualMachine.m_Logging.SetIPULoggingStatus(!m_virtualMachine.m_Logging.GetIPULoggingStatus());
		UpdateLoggingMenu();
		break;
	case ID_LOGGING_OS:
//		m_virtualMachine.m_Logging.SetOSLoggingStatus(!m_virtualMachine.m_Logging.GetOSLoggingStatus());
		UpdateLoggingMenu();
		break;
	case ID_LOGGING_SIF:
//		m_virtualMachine.m_Logging.SetSIFLoggingStatus(!m_virtualMachine.m_Logging.GetSIFLoggingStatus());
		UpdateLoggingMenu();
		break;
	case ID_LOGGING_IOP:
//		m_virtualMachine.m_Logging.SetIOPLoggingStatus(!m_virtualMachine.m_Logging.GetIOPLoggingStatus());
		UpdateLoggingMenu();
		break;
	case ID_LOGGING_SAVEVPUSTATE:
#ifdef DEBUGGER_INCLUDED
		m_virtualMachine.SetSaveVpuStateEnabled(!m_virtualMachine.IsSaveVpuStateEnabled());
		UpdateLoggingMenu();
#endif
		break;
	case ID_WINDOW_CASCAD:
		m_pMDIClient->Cascade();
		return FALSE;
		break;
	case ID_WINDOW_TILEHORIZONTAL:
		m_pMDIClient->TileHorizontal();
		return FALSE;
		break;
	case ID_WINDOW_TILEVERTICAL:
		m_pMDIClient->TileVertical();
		return FALSE;
		break;
	case ID_WINDOW_LAYOUT1024:
		Layout1024();
		return FALSE;
		break;
	case ID_WINDOW_LAYOUT1280:
		Layout1280();
		return FALSE;
		break;
	case ID_WINDOW_LAYOUT1600:
		Layout1600();
		return FALSE;
		break;
	case ID_EDIT_COPY:
		SendMessage(m_pMDIClient->GetActiveWindow(), WM_COPY, 0, 0);
		break;
	}
	return TRUE;
}

long CDebugger::OnSysCommand(unsigned int nCmd, LPARAM lParam)
{
	switch(nCmd)
	{
	case SC_CLOSE:
		Show(SW_HIDE);
		return FALSE;
	}
	return TRUE;
}

long CDebugger::OnWndProc(unsigned int nMsg, WPARAM wParam, LPARAM lParam)
{
	switch(nMsg)
	{
	case WM_EXECUNLOAD:
		OnExecutableUnloadingMsg();
		return FALSE;
		break;
	case WM_EXECCHANGE:
		OnExecutableChangeMsg();
		return FALSE;
		break;
	}
	return CMDIFrame::OnWndProc(nMsg, wParam, lParam);
}

void CDebugger::OnFunctionsViewFunctionDblClick(uint32 nAddress)
{
	GetDisassemblyWindow()->SetAddress(nAddress);
}

void CDebugger::OnFunctionsViewFunctionsStateChange()
{
	GetDisassemblyWindow()->Refresh();
}

void CDebugger::OnThreadsViewAddressDblClick(uint32 nAddress)
{
	GetDisassemblyWindow()->SetCenterAtAddress(nAddress);
	GetDisassemblyWindow()->SetSelectedAddress(nAddress);
}

void CDebugger::OnExecutableChange()
{
	SendMessage(m_hWnd, WM_EXECCHANGE, 0, 0);
}

void CDebugger::OnExecutableUnloading()
{
	SendMessage(m_hWnd, WM_EXECUNLOAD, 0, 0);
}

void CDebugger::OnExecutableChangeMsg()
{
	m_pELFView->SetELF(m_virtualMachine.m_os->GetELF());
//	m_pFunctionsView->SetELF(m_virtualMachine.m_os->GetELF());

	LoadDebugTags();

	GetDisassemblyWindow()->Refresh();
	m_pFunctionsView->Refresh();
}

void CDebugger::OnExecutableUnloadingMsg()
{
	SaveDebugTags();
	m_pELFView->SetELF(NULL);
//	m_pFunctionsView->SetELF(NULL);
}

void CDebugger::LoadDebugTags()
{
#ifdef DEBUGGER_INCLUDED
	m_virtualMachine.LoadDebugTags(m_virtualMachine.m_os->GetExecutableName());
#endif
}

void CDebugger::SaveDebugTags()
{
#ifdef DEBUGGER_INCLUDED
	if(m_virtualMachine.m_os->GetELF() != NULL)
	{
		m_virtualMachine.SaveDebugTags(m_virtualMachine.m_os->GetExecutableName());
	}
#endif
}
