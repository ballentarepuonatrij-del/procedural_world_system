#include "planet/win32_dialogs.hpp"
#include "planet/state.hpp"
#include <commctrl.h>
#include <shlobj.h>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace planet {
namespace {
constexpr int moduleId=100,gridId=moduleId+moduleCount,volumeId=gridId+1,volumeTextId=gridId+2,interfaceId=gridId+3,skyId=gridId+4,shadowsId=gridId+5;
constexpr int seedId=200,gammaId=220,brightnessId=221,gammaTextId=222,brightnessTextId=223,soundId=225,resetId=226,tabId=300,statusId=301,saveId=400,loadId=420,slotTextId=440,loadedResult=10;
constexpr int moduleRows=(moduleCount+1)/2;
struct DialogData {
    Preferences preferences;
    bool seedOnly=false,previewPending=false;
    SettingsActions actions;
    std::function<void(std::uint64_t)> seedCommit;
    std::vector<std::pair<HWND,int>> pages;
};
std::vector<WORD> dialogTemplate(bool seed) {
    const DLGTEMPLATE header{WS_POPUP|WS_CAPTION|WS_SYSMENU|DS_MODALFRAME|DS_CENTER|DS_SETFONT,0,0,0,0,
        static_cast<short>(seed ? 320 : 440),static_cast<short>(seed ? 134 : 322)};
    std::vector<WORD> data((sizeof(header)+1)/2);std::memcpy(data.data(),&header,sizeof(header));
    data.insert(data.end(),{0,0,0,10});
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
Preferences readControls(HWND dialog,const DialogData& data) {
    auto next=data.preferences;
    for(unsigned i=0;i<moduleCount;++i) next.modules.set(static_cast<Module>(i),IsDlgButtonChecked(dialog,moduleId+static_cast<int>(i))==BST_CHECKED);
    next.grid=IsDlgButtonChecked(dialog,gridId)==BST_CHECKED;
    next.interfaceVisible=IsDlgButtonChecked(dialog,interfaceId)==BST_CHECKED;
    next.sky=IsDlgButtonChecked(dialog,skyId)==BST_CHECKED;next.shadows=IsDlgButtonChecked(dialog,shadowsId)==BST_CHECKED;
    next.volume=static_cast<unsigned>(SendDlgItemMessageW(dialog,volumeId,TBM_GETPOS,0,0));
    next.gamma=static_cast<unsigned>(SendDlgItemMessageW(dialog,gammaId,TBM_GETPOS,0,0));
    next.brightness=static_cast<unsigned>(SendDlgItemMessageW(dialog,brightnessId,TBM_GETPOS,0,0));return next;
}
void sliderLabels(HWND dialog) {
    for(const auto& [slider,label]:std::array{std::pair{volumeId,volumeTextId},std::pair{gammaId,gammaTextId},std::pair{brightnessId,brightnessTextId}}) {
        const auto value=SendDlgItemMessageW(dialog,slider,TBM_GETPOS,0,0);
        const auto text=slider==gammaId ? std::to_wstring(value/100)+L","+(value%100<10 ? L"0" : L"")+std::to_wstring(value%100) : std::to_wstring(value)+L" %";
        SetDlgItemTextW(dialog,label,text.c_str());
    }
}
void requestPreview(HWND dialog,DialogData& data) {
    // Coalesce slider input instead of rendering once per queued mouse/key event.
    if(!data.previewPending) {
        if(!SetTimer(dialog,1,50,nullptr)) throw std::runtime_error("Cannot schedule settings preview");
        data.previewPending=true;
    }
}
void page(HWND dialog,const DialogData& data) {
    const int current=TabCtrl_GetCurSel(GetDlgItem(dialog,tabId));
    for(const auto& [window,index]:data.pages) ShowWindow(window,index==current ? SW_SHOW : SW_HIDE);
    InvalidateRect(dialog,nullptr,TRUE);
}
void refreshSlots(HWND dialog,const DialogData& data) {
    for(unsigned i=0;i<saveSlotCount;++i) {
        const auto info=data.actions.slot(i);
        SetDlgItemTextW(dialog,slotTextId+static_cast<int>(i),info.description.c_str());
        EnableWindow(GetDlgItem(dialog,loadId+static_cast<int>(i)),info.available);
    }
}
void errorMessage(HWND dialog,const std::exception& e) {
    const std::string text=e.what();const std::wstring message=L"Не удалось выполнить действие.\n\n"+std::wstring(text.begin(),text.end());
    MessageBoxW(dialog,message.c_str(),L"PlanetLab — Ошибка",MB_OK|MB_ICONERROR);
}
LRESULT CALLBACK seedEdit(HWND window,UINT message,WPARAM wParam,LPARAM lParam,UINT_PTR id,DWORD_PTR) {
    if(message==WM_KEYDOWN && wParam=='A' && (GetKeyState(VK_CONTROL)&0x8000)) {SendMessageW(window,EM_SETSEL,0,-1);return 0;}
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
            const auto tab=control(dialog,WC_TABCONTROLW,L"",WS_TABSTOP|WS_CLIPSIBLINGS,tabId,8,8,424,272);
            constexpr std::array titles{L"Звук",L"Графика",L"Модули",L"Интерфейс",L"Горячие клавиши",L"Сохранения"};
            for(unsigned i=0;i<titles.size();++i) {TCITEMW item{};item.mask=TCIF_TEXT;item.pszText=const_cast<wchar_t*>(titles[i]);SendMessageW(tab,TCM_INSERTITEMW,i,reinterpret_cast<LPARAM>(&item));}
            const auto add=[&](int p,const wchar_t* type,const wchar_t* text,DWORD style,int id,int x,int y,int w,int h) {
                const auto window=control(dialog,type,text,style,id,x,y,w,h);data->pages.emplace_back(window,p);return window;
            };
            const auto check=[&](int p,const wchar_t* text,int id,int x,int y,int w,bool checked) {add(p,L"BUTTON",text,WS_TABSTOP|BS_AUTOCHECKBOX,id,x,y,w,18);CheckDlgButton(dialog,id,checked ? BST_CHECKED : BST_UNCHECKED);};
            const auto slider=[&](int p,const wchar_t* text,int id,int label,int y,unsigned value,int low,int high) {
                add(p,L"STATIC",text,0,-1,20,y+5,75,18);add(p,TRACKBAR_CLASSW,L"",WS_TABSTOP|TBS_HORZ|TBS_NOTICKS,id,99,y,244,25);
                SendDlgItemMessageW(dialog,id,TBM_SETRANGE,TRUE,MAKELPARAM(low,high));SendDlgItemMessageW(dialog,id,TBM_SETPOS,TRUE,value);add(p,L"STATIC",L"",0,label,354,y+5,56,18);
            };
            check(0,L"Включить звук",soundId,20,46,380,data->preferences.modules.requested(Module::Sound));
            slider(0,L"Громкость",volumeId,volumeTextId,83,data->preferences.volume,0,100);
            add(0,L"STATIC",L"Процедурные звуки ветра, воды, погоды и окружающего мира.\nОК сохраняет выбранную громкость для следующего запуска.",0,-1,20,130,390,48);
            check(1,L"Grid — сетка ландшафта",gridId,20,44,390,data->preferences.grid);
            check(1,L"Небо и звёзды",skyId,20,66,390,data->preferences.sky);
            check(1,L"Тени объектов и рельефа",shadowsId,20,88,390,data->preferences.shadows);
            slider(1,L"Гамма",gammaId,gammaTextId,120,data->preferences.gamma,50,250);
            slider(1,L"Яркость",brightnessId,brightnessTextId,155,data->preferences.brightness,50,150);
            add(1,L"BUTTON",L"Гамма и яркость по умолчанию",WS_TABSTOP,resetId,20,198,210,22);
            add(1,L"STATIC",L"Предпросмотр применяется к миру. Отмена вернёт прежние значения.\nПо умолчанию: гамма 1,00; яркость 100 %; Grid выключена.",0,-1,20,234,390,35);
            constexpr std::array<const wchar_t*,moduleCount> labels{L"Рельеф",L"Океан",L"Атмосфера",L"Климат",L"Биомы",L"Детали поверхности",L"Ветер",L"Облака",L"Поверхностная вода",L"Эрозия",L"Формы рельефа",L"Пещеры",L"Волны и течения",L"Рост растений",L"Глобальная погода",L"Звук",L"Реки",L"Озёра и болота",L"Животные",L"Снег",L"Атмосферные явления"};
            for(unsigned i=0;i<moduleCount;++i) check(2,labels[i],moduleId+static_cast<int>(i),20+static_cast<int>(i/moduleRows)*202,44+static_cast<int>(i%moduleRows)*17,194,data->preferences.modules.requested(static_cast<Module>(i)));
            add(2,L"STATIC",L"Зависимые модули работают, когда включены их основы.\nБазовая планета всегда активна.",0,-1,20,242,390,32);
            check(3,L"Показывать интерфейс (Ctrl+U)",interfaceId,20,46,390,data->preferences.interfaceVisible);
            add(3,L"STATIC",L"H — краткие подсказки управления.\nE — сведения о животных и растениях.\n\nEsc освобождает курсор; ещё одно нажатие открывает настройки.\nКлик по миру возвращает управление взглядом.\n\nОкно при запуске занимает 80 % рабочей области экрана\nи располагается по центру.",0,-1,20,85,390,170);
            add(4,L"EDIT",L"W/A/S/D — движение; мышь — взгляд\r\nShift — быстрее; Space — прыжок / вверх; Ctrl — вниз\r\nF — полёт; колесо — скорость полёта\r\nEsc — освободить курсор / настройки; Ctrl+U — интерфейс\r\nF5 — быстрое сохранение; F9 — быстрая загрузка\r\nCtrl+S — Seed; Tab — глобус (ПКМ — вращать, ЛКМ — перейти)\r\nP — пауза; +/- — скорость времени; T — полдень\r\nH — подсказки; E — растения / животные; L — фонарь\r\nV — частицы ветра; U — маркеры течений\r\nF2 — штормы; F3 — слой глобуса; F4 — болота / озёра / пруды\r\nF6 — атмосферные явления; F7 — реки; F8 — луг\r\nF10 — побережья; F11 — олени; F12 — снег\r\nJ — горы / каньоны / пещеры; X — леса; O — океан\r\nHome — посадка на сушу; N — опыт с ветром; B — дождь\r\nY — ускорение роста; Z — молодые растения",WS_TABSTOP|WS_VSCROLL|ES_MULTILINE|ES_READONLY,310,20,44,396,224);
            add(5,L"STATIC",L"Снимок мира: Seed, исследователь, время и состояние симуляции.",0,-1,20,43,396,18);
            for(unsigned i=0;i<saveSlotCount;++i) {
                const int y=67+static_cast<int>(i)*23;
                const auto name=i<manualSlotCount ? L"Слот "+std::to_wstring(i+1) : (i==quickSlot ? std::wstring(L"Быстрый") : std::wstring(L"Авто"));
                add(5,L"STATIC",name.c_str(),0,-1,20,y+3,56,18);add(5,L"STATIC",L"",0,slotTextId+static_cast<int>(i),78,y+1,214,22);
                if(i!=autoSlot) add(5,L"BUTTON",L"Сохранить",WS_TABSTOP,saveId+static_cast<int>(i),296,y,58,21);
                else add(5,L"STATIC",L"При выходе",0,-1,296,y+3,58,18);
                add(5,L"BUTTON",L"Загрузить",WS_TABSTOP,loadId+static_cast<int>(i),360,y,58,21);
            }
            add(5,L"STATIC",L"Слоты записываются сразу. Загрузка закрывает настройки.",0,-1,20,254,396,18);
            control(dialog,L"STATIC",L"ОК сохраняет настройки. Отмена отменяет предпросмотр.",0,statusId,12,290,268,25);
            control(dialog,L"BUTTON",L"ОК",WS_TABSTOP|BS_DEFPUSHBUTTON,IDOK,292,290,64,22);
            control(dialog,L"BUTTON",L"Отмена",WS_TABSTOP,IDCANCEL,364,290,64,22);
            sliderLabels(dialog);refreshSlots(dialog,*data);page(dialog,*data);return TRUE;
        }
        if(!data) return FALSE;
        if(message==WM_NOTIFY && reinterpret_cast<NMHDR*>(lParam)->idFrom==tabId && reinterpret_cast<NMHDR*>(lParam)->code==TCN_SELCHANGE) {page(dialog,*data);return TRUE;}
        if(message==WM_HSCROLL) {sliderLabels(dialog);requestPreview(dialog,*data);return TRUE;}
        if(message==WM_TIMER && wParam==1) {KillTimer(dialog,1);data->previewPending=false;data->actions.preview(readControls(dialog,*data));return TRUE;}
        if(message==WM_CLOSE || (message==WM_COMMAND && LOWORD(wParam)==IDCANCEL)) {EndDialog(dialog,IDCANCEL);return TRUE;}
        if(message==WM_COMMAND) {
            const int id=LOWORD(wParam);
            if(!data->seedOnly && HIWORD(wParam)==BN_CLICKED) {
                const int soundModuleId=moduleId+static_cast<int>(Module::Sound);
                if(id==soundId || id==soundModuleId) CheckDlgButton(dialog,id==soundId ? soundModuleId : soundId,IsDlgButtonChecked(dialog,id));
                if(id==resetId) {SendDlgItemMessageW(dialog,gammaId,TBM_SETPOS,TRUE,100);SendDlgItemMessageW(dialog,brightnessId,TBM_SETPOS,TRUE,100);sliderLabels(dialog);requestPreview(dialog,*data);return TRUE;}
                if(id>=saveId && id<saveId+static_cast<int>(autoSlot)) {data->actions.save(static_cast<unsigned>(id-saveId));refreshSlots(dialog,*data);SetDlgItemTextW(dialog,statusId,L"Состояние мира сохранено. ОК сохраняет настройки.");return TRUE;}
                if(id>=loadId && id<loadId+static_cast<int>(saveSlotCount)) {data->actions.load(static_cast<unsigned>(id-loadId));EndDialog(dialog,loadedResult);return TRUE;}
            }
            if(id==IDOK) {
                if(data->seedOnly) {
                    wchar_t value[128]{};GetDlgItemTextW(dialog,seedId,value,128);const std::wstring text=value;
                    const bool ascii=std::all_of(text.begin(),text.end(),[](wchar_t ch){return ch>=L'0' && ch<=L'9';});
                    const auto seed=ascii ? parseSeed(std::string(text.begin(),text.end())) : std::nullopt;
                    if(!seed) {MessageBoxW(dialog,L"Введите целое число от 0 до 18446744073709551615 без знака и пробелов.",L"PlanetLab — Неверный Seed",MB_OK|MB_ICONWARNING);SetFocus(GetDlgItem(dialog,seedId));SendDlgItemMessageW(dialog,seedId,EM_SETSEL,0,-1);return TRUE;}
                    data->seedCommit(*seed);
                } else data->actions.commit(readControls(dialog,*data));
                EndDialog(dialog,IDOK);return TRUE;
            }
        }
    } catch(const std::exception& e) {errorMessage(dialog,e);if(message==WM_INITDIALOG) EndDialog(dialog,IDCANCEL);return TRUE;}
    return FALSE;
}
INT_PTR show(HWND owner,DialogData& data) {
    const INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX),ICC_BAR_CLASSES|ICC_TAB_CLASSES};
    if(!InitCommonControlsEx(&controls)) throw std::runtime_error("Cannot initialize settings controls");
    const auto layout=dialogTemplate(data.seedOnly);
    const auto result=DialogBoxIndirectParamW(GetModuleHandleW(nullptr),reinterpret_cast<const DLGTEMPLATE*>(layout.data()),owner,procedure,reinterpret_cast<LPARAM>(&data));
    if(result==-1) throw std::runtime_error("Cannot open dialog");
    return result;
}
}
std::filesystem::path defaultPreferencesPath() {
    wchar_t folder[MAX_PATH]{};
    if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,folder))) throw std::runtime_error("Cannot locate the Windows settings folder");
    return std::filesystem::path(folder)/L"PlanetLab"/L"settings.ini";
}
SettingsResult settingsDialog(HWND owner,const Preferences& initial,const SettingsActions& actions) {
    DialogData data;data.preferences=initial;data.actions=actions;const auto result=show(owner,data);
    return result==IDOK ? SettingsResult::Applied : result==loadedResult ? SettingsResult::Loaded : SettingsResult::Cancelled;
}
bool seedDialog(HWND owner,std::uint64_t initial,const std::function<void(std::uint64_t)>& commit) {
    DialogData data;data.seedOnly=true;data.preferences.seed=initial;data.seedCommit=commit;return show(owner,data)==IDOK;
}
} // namespace planet
