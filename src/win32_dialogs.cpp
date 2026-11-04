#include "planet/win32_dialogs.hpp"
#include <commctrl.h>
#include <shlobj.h>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace planet {
namespace {
constexpr int moduleId=100,gridId=120,volumeId=121,volumeTextId=122,interfaceId=123,seedId=200;
struct DialogData {
    Preferences preferences;
    bool seedOnly=false;
    std::function<void(const Preferences&)> settingsCommit;
    std::function<void(std::uint64_t)> seedCommit;
};
std::vector<WORD> dialogTemplate(bool seed) {
    const DLGTEMPLATE header{WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_CENTER|DS_SETFONT,0,0,0,0,
        static_cast<short>(seed ? 320 : 360),static_cast<short>(seed ? 134 : 302)};
    std::vector<WORD> data((sizeof(header)+1)/2);std::memcpy(data.data(),&header,sizeof(header));
    data.insert(data.end(),{0,0,0,10}); // No menu/class/title; 10 pt font follows.
    for(wchar_t ch:std::wstring(L"Segoe UI")) data.push_back(static_cast<WORD>(ch));
    data.push_back(0);return data;
}
HWND control(HWND parent,const wchar_t* type,const wchar_t* text,DWORD style,int id,int x,int y,int width,int height) {
    RECT r{x,y,x+width,y+height};MapDialogRect(parent,&r);
    HWND result=CreateWindowExW(type==std::wstring_view(L"EDIT") ? WS_EX_CLIENTEDGE : 0,type,text,
        WS_CHILD|WS_VISIBLE|style,r.left,r.top,r.right-r.left,r.bottom-r.top,parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    if(!result) throw std::runtime_error("Cannot create dialog control");
    SendMessageW(result,WM_SETFONT,SendMessageW(parent,WM_GETFONT,0,0),TRUE);return result;
}
void volumeLabel(HWND dialog) {
    const auto value=SendDlgItemMessageW(dialog,volumeId,TBM_GETPOS,0,0);
    SetDlgItemTextW(dialog,volumeTextId,(std::to_wstring(value)+L" %").c_str());
}
void errorMessage(HWND dialog,const std::exception& e) {
    const std::string text=e.what();
    const std::wstring message=L"Не удалось применить настройки. Изменения не сохранены.\n\n"+std::wstring(text.begin(),text.end());
    MessageBoxW(dialog,message.c_str(),L"PlanetLab — Ошибка",MB_OK|MB_ICONERROR);
}
LRESULT CALLBACK seedEdit(HWND window,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR id,DWORD_PTR) {
    if(message==WM_KEYDOWN && wParam=='A' && (GetKeyState(VK_CONTROL)&0x8000)) {
        SendMessageW(window,EM_SETSEL,0,-1);return 0;
    }
    if(message==WM_CHAR && wParam==1) return 0;
    if(message==WM_NCDESTROY) RemoveWindowSubclass(window,seedEdit,id);
    return DefSubclassProc(window,message,wParam,lParam);
}
INT_PTR CALLBACK procedure(HWND dialog,UINT message,WPARAM wParam,LPARAM lParam) {
    auto* data=reinterpret_cast<DialogData*>(GetWindowLongPtrW(dialog,DWLP_USER));
    try {
        if(message==WM_INITDIALOG) {
            data=reinterpret_cast<DialogData*>(lParam);SetWindowLongPtrW(dialog,DWLP_USER,reinterpret_cast<LONG_PTR>(data));
            SetWindowTextW(dialog,data->seedOnly ? L"PlanetLab — Seed" : L"PlanetLab — Настройки");
            if(data->seedOnly) {
                control(dialog,L"STATIC",L"Seed: целое число от 0 до 18446744073709551615",0,-1,12,12,296,16);
                const auto edit=control(dialog,L"EDIT",std::to_wstring(data->preferences.seed).c_str(),WS_TABSTOP|ES_AUTOHSCROLL,seedId,12,32,296,22);
                if(!SetWindowSubclass(edit,seedEdit,1,0)) throw std::runtime_error("Cannot initialize seed editor");
                SendMessageW(edit,EM_SETLIMITTEXT,64,0);SendMessageW(edit,EM_SETSEL,0,-1);
                control(dialog,L"STATIC",L"Смена seed создаст новый мир и сбросит его текущее состояние. Настройки модулей сохранятся.",0,-1,12,63,296,30);
                control(dialog,L"BUTTON",L"ОК",WS_TABSTOP|BS_DEFPUSHBUTTON,IDOK,170,103,64,22);
                control(dialog,L"BUTTON",L"Отмена",WS_TABSTOP,IDCANCEL,244,103,64,22);
                SetFocus(edit);return FALSE;
            }
            control(dialog,L"STATIC",L"Модули симуляции",0,-1,12,10,336,16);
            constexpr std::array<const wchar_t*,moduleCount> labels{L"Рельеф",L"Океан",L"Атмосфера",L"Климат",L"Биомы",L"Детали поверхности",L"Ветер",L"Облака",L"Поверхностная вода",L"Эрозия",L"Формы рельефа",L"Пещеры",L"Волны и течения",L"Рост растений",L"Глобальная погода",L"Звук"};
            for(unsigned i=0;i<moduleCount;++i) {
                const auto id=moduleId+static_cast<int>(i);
                control(dialog,L"BUTTON",labels[i],WS_TABSTOP|BS_AUTOCHECKBOX,id,12+static_cast<int>(i/8)*174,30+static_cast<int>(i%8)*19,162,17);
                CheckDlgButton(dialog,id,data->preferences.modules.requested(static_cast<Module>(i)) ? BST_CHECKED : BST_UNCHECKED);
            }
            control(dialog,L"STATIC",L"Зависимые модули работают, когда включены их основы. Базовая планета всегда активна.",0,-1,12,185,336,28);
            control(dialog,L"BUTTON",L"Grid — сетка ландшафта",WS_TABSTOP|BS_AUTOCHECKBOX,gridId,12,218,165,18);
            CheckDlgButton(dialog,gridId,data->preferences.grid ? BST_CHECKED : BST_UNCHECKED);
            control(dialog,L"BUTTON",L"Показывать интерфейс",WS_TABSTOP|BS_AUTOCHECKBOX,interfaceId,186,218,162,18);
            CheckDlgButton(dialog,interfaceId,data->preferences.interfaceVisible ? BST_CHECKED : BST_UNCHECKED);
            control(dialog,L"STATIC",L"Громкость",0,-1,12,245,65,18);
            control(dialog,TRACKBAR_CLASSW,L"",WS_TABSTOP|TBS_HORZ|TBS_NOTICKS,volumeId,78,240,214,25);
            SendDlgItemMessageW(dialog,volumeId,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));
            SendDlgItemMessageW(dialog,volumeId,TBM_SETPOS,TRUE,data->preferences.volume);
            control(dialog,L"STATIC",L"",0,volumeTextId,300,245,48,18);volumeLabel(dialog);
            control(dialog,L"STATIC",L"ОК сохраняет настройки для следующего запуска.",0,-1,12,276,192,20);
            control(dialog,L"BUTTON",L"ОК",WS_TABSTOP|BS_DEFPUSHBUTTON,IDOK,212,272,64,22);
            control(dialog,L"BUTTON",L"Отмена",WS_TABSTOP,IDCANCEL,284,272,64,22);
            return TRUE;
        }
        if(!data) return FALSE;
        if(message==WM_HSCROLL) {volumeLabel(dialog);return TRUE;}
        if(message==WM_CLOSE || (message==WM_COMMAND && LOWORD(wParam)==IDCANCEL)) {EndDialog(dialog,IDCANCEL);return TRUE;}
        if(message==WM_COMMAND && LOWORD(wParam)==IDOK) {
            if(data->seedOnly) {
                wchar_t value[128]{};GetDlgItemTextW(dialog,seedId,value,128);const std::wstring text=value;
                const bool ascii=std::all_of(text.begin(),text.end(),[](wchar_t ch){return ch>=L'0' && ch<=L'9';});
                const auto seed=ascii ? parseSeed(std::string(text.begin(),text.end())) : std::nullopt;
                if(!seed) {
                    MessageBoxW(dialog,L"Введите целое число от 0 до 18446744073709551615 без знака и пробелов.",L"PlanetLab — Неверный Seed",MB_OK|MB_ICONWARNING);
                    SetFocus(GetDlgItem(dialog,seedId));SendDlgItemMessageW(dialog,seedId,EM_SETSEL,0,-1);return TRUE;
                }
                data->seedCommit(*seed);
            } else {
                auto next=data->preferences;
                for(unsigned i=0;i<moduleCount;++i) next.modules.set(static_cast<Module>(i),IsDlgButtonChecked(dialog,moduleId+static_cast<int>(i))==BST_CHECKED);
                next.grid=IsDlgButtonChecked(dialog,gridId)==BST_CHECKED;
                next.interfaceVisible=IsDlgButtonChecked(dialog,interfaceId)==BST_CHECKED;
                next.volume=static_cast<unsigned>(SendDlgItemMessageW(dialog,volumeId,TBM_GETPOS,0,0));
                data->settingsCommit(next);
            }
            EndDialog(dialog,IDOK);return TRUE;
        }
    } catch(const std::exception& e) {
        errorMessage(dialog,e);if(message==WM_INITDIALOG) EndDialog(dialog,IDCANCEL);
        return TRUE;
    }
    return FALSE;
}
bool show(HWND owner,DialogData& data) {
    const INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX),ICC_BAR_CLASSES};
    if(!InitCommonControlsEx(&controls)) throw std::runtime_error("Cannot initialize settings controls");
    const auto layout=dialogTemplate(data.seedOnly);
    const auto result=DialogBoxIndirectParamW(GetModuleHandleW(nullptr),reinterpret_cast<const DLGTEMPLATE*>(layout.data()),owner,procedure,reinterpret_cast<LPARAM>(&data));
    if(result==-1) throw std::runtime_error("Cannot open dialog");
    return result==IDOK;
}
}
std::filesystem::path defaultPreferencesPath() {
    wchar_t folder[MAX_PATH]{};
    if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,folder)))
        throw std::runtime_error("Cannot locate the Windows settings folder");
    return std::filesystem::path(folder)/L"PlanetLab"/L"settings.ini";
}
bool settingsDialog(HWND owner,const Preferences& initial,const std::function<void(const Preferences&)>& commit) {
    DialogData data;data.preferences=initial;data.settingsCommit=commit;return show(owner,data);
}
bool seedDialog(HWND owner,std::uint64_t initial,const std::function<void(std::uint64_t)>& commit) {
    DialogData data;data.seedOnly=true;data.preferences.seed=initial;data.seedCommit=commit;return show(owner,data);
}
} // namespace planet
