/*
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see http://www.gnu.org/licenses/
 */

#include "gme_game.h"
#include "gme_mods.h"
#include "gme_repo.h"
#include "gme_tools.h"
#include "gme_netw.h"
#include "gme_logs.h"
#include "pugixml.hpp"

using namespace pugi;

/* simple struct for URL entry */
struct GME_Repos_Struct
{
  bool enabled;
  int type; /* not currently used */
  char url[256];
};


/*
  utility function to parse version from string
*/
inline GME_ModVers_Struct GME_RepoParseVers(const std::wstring& str)
{
  GME_ModVers_Struct version;

  version.major = 0;
  version.minor = 0;
  version.revis = 0;

  std::vector<std::wstring> numbers;
  GME_StrSplit(str, &numbers, L".");

  if(!numbers.empty()) {
    version.major = wcstol(numbers[0].c_str(), NULL, 10);
    if(numbers.size() > 1) {
      version.minor = wcstol(numbers[1].c_str(), NULL, 10);
      if(numbers.size() > 2) {
        version.revis = wcstol(numbers[2].c_str(), NULL, 10);
      }
    }
  }

  return version;
}

/*
  utility function to parse version to string
*/
inline std::wstring GME_RepoVersString(const GME_ModVers_Struct& vers)
{
  std::wstring str;

  wchar_t buff[64];
  swprintf(buff, 64, L"%d", vers.major);
  str = buff;
  if(vers.minor >= 0) {
    swprintf(buff, 64, L".%d", vers.minor);
    str += buff;
    if(vers.revis >= 0) {
      swprintf(buff, 64, L".%d", vers.revis);
      str += buff;
    }
  }

  return str;
}

/*
  operator == overload for version comparison
*/
bool operator==(const GME_ModVers_Struct& left, const GME_ModVers_Struct& right)
{
  return (left.major == right.major && left.minor == right.minor && left.revis == right.revis);
}

/*
  operator == overload for version comparison
*/
bool operator!=(const GME_ModVers_Struct& left, const GME_ModVers_Struct& right)
{
  return (left.major != right.major || left.minor != right.minor || left.revis != right.revis);
}

/*
  operator > overload for version comparison
*/
bool operator>(const GME_ModVers_Struct& left, const GME_ModVers_Struct& right)
{
  if(left.major > right.major) {
    return true;
  } else {
    if(left.minor > right.minor) {
      return true;
    } else {
      if(left.revis > right.revis)
        return true;
    }
  }
  return false;
}

/*
  operator > overload for version comparison
*/
bool operator<(const GME_ModVers_Struct& left, const GME_ModVers_Struct& right)
{
  if(left.major < right.major) {
    return true;
  } else {
    if(left.minor < right.minor) {
      return true;
    } else {
      if(left.revis < right.revis)
        return true;
    }
  }
  return false;
}

/* repository list */
std::vector<GME_Repos_Struct> g_GME_Repos_List;

/* mod repo info struct */
struct GME_ReposMod_Struct
{
  GME_ReposMod_Struct() {
    memset(name, 0, 255*sizeof(wchar_t));
    memset(url, 0, 255);
    memset(&version, 0, sizeof(GME_ModVers_Struct));
  }

  clear() {
    memset(name, 0, 255*sizeof(wchar_t));
    memset(url, 0, 255);
    memset(&version, 0, sizeof(GME_ModVers_Struct));
    desc.clear();
  }

  wchar_t name[255];

  char url[255];

  GME_ModVers_Struct version;

  std::string desc;

};

std::vector<GME_ReposMod_Struct> g_GME_ReposMod_List;
std::vector<GME_ReposMod_Struct> g_GME_ReposDnl_List;

/* thread id for repos query */
HANDLE g_ReposQry_hT;
DWORD g_ReposQry_iT;
bool g_ReposQry_Running;
bool g_ReposQry_Cancel;
unsigned g_ReposQry_Id;


/*
  function to safely clean threads and/or memory usage by mods process
*/
void GME_RepoClean()
{
  GME_RepoQueryCancel();
}


/*
  Write repository list
*/
bool GME_RepoWritList()
{
  if(GME_GameGetCurId() == -1)
    return false;

  std::wstring cfg_path = GME_GameGetCurConfPath() + L"\\repos.dat";

  if(!g_GME_Repos_List.empty()) {
    FILE* fp = _wfopen(cfg_path.c_str(), L"wb");
    if(fp) {
      /* first 4 bytes is count */
      unsigned c = g_GME_Repos_List.size();
      fwrite(&c, sizeof(unsigned), 1, fp);
      for(unsigned i = 0; i < c; i++) {
        fwrite(&g_GME_Repos_List[i], sizeof(GME_Repos_Struct), 1, fp);
      }
      fclose(fp);
      return true;
    }
  } else {
    GME_FileDelete(cfg_path);
    return true;
  }
  return false;
}

/*
  Read repository list
*/
bool GME_RepoReadList()
{
  if(GME_GameGetCurId() == -1)
    return false;

  g_GME_Repos_List.clear();

  std::wstring cfg_path = GME_GameGetCurConfPath() + L"\\repos.dat";
  GME_Repos_Struct repos;

  FILE* fp = _wfopen(cfg_path.c_str(), L"rb");
  if(fp) {
    unsigned c;
    /* first 4 bytes is count */
    fread(&c, 1, 4, fp);
    for(unsigned i = 0; i < c; i++) {
      memset(&repos, 0, sizeof(GME_Repos_Struct));
      fread(&repos, 1, sizeof(GME_Repos_Struct), fp);
      g_GME_Repos_List.push_back(repos);
    }
    fclose(fp);
    return true;
  }
  return false;
}

bool GME_RepoChkList()
{
  if(g_GME_Repos_List.empty())
    return false;

  HWND htv = GetDlgItem(g_hwndRepConf, TVS_REPOSLIST);

  char buff[256];
  TVITEM item;
  memset(&item, 0, sizeof(TVITEM));
  item.mask = TVIF_TEXT;
  item.cchTextMax = 256;
  item.pszText = buff;

  item.hItem = (HTREEITEM)SendMessage(htv, TVM_GETNEXTITEM, (WPARAM)TVGN_ROOT, (LPARAM)NULL);

  SendMessage(htv, TVM_GETITEM, 0, (LPARAM)&item);

  int imgstate;

  for(unsigned i = 0; i < g_GME_Repos_List.size(); i++) {
    if(!strcmp(g_GME_Repos_List[i].url, item.pszText)) {
      imgstate = SendMessage(htv, TVM_GETITEMSTATE, (WPARAM)item.hItem, TVIS_STATEIMAGEMASK);
      g_GME_Repos_List[i].enabled = (0x2000 & imgstate); /* 0x2000 is the bit for check-box checked image */
      break;
    }
  }

  while(item.hItem = TreeView_GetNextItem(htv, item.hItem, TVGN_NEXT)) {
    SendMessage(htv, TVM_GETITEM, 0, (LPARAM)&item);
    for(unsigned i = 0; i < g_GME_Repos_List.size(); i++) {
      if(!strcmp(g_GME_Repos_List[i].url, item.pszText)) {
        imgstate = SendMessage(htv, TVM_GETITEMSTATE, (WPARAM)item.hItem, TVIS_STATEIMAGEMASK);
        g_GME_Repos_List[i].enabled = (0x2000 & imgstate); /* 0x2000 is the bit for check-box checked image */
        break;
      }
    }
  }

  return true;
}

bool GME_RepoUpdList()
{
  HWND htv = GetDlgItem(g_hwndRepConf, TVS_REPOSLIST);
  /* delete all items in list */
  SendMessage(htv, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);

  if(GME_GameGetCurId() == -1) {
    GME_DialogError(g_hwndRepConf, L"Игра не выбрана.");
    return false;
  }

  if(!GME_IsFile(GME_GameGetCurConfPath() + L"\\repos.dat"))
    return false;


  if(!GME_RepoReadList()) {
    GME_DialogError(g_hwndRepConf, L"Не удалось прочитать файл настроек репозиториев.");
    return false;
  }

  TVINSERTSTRUCT tvins;
  memset(&tvins, 0, sizeof(TVINSERTSTRUCT));
  tvins.hInsertAfter = TVI_LAST;
  tvins.item.mask = TVIF_TEXT|TVIF_STATE;
  tvins.item.cchTextMax = 255;
  tvins.item.stateMask = TVIS_STATEIMAGEMASK;

  for(unsigned i = 0; i < g_GME_Repos_List.size(); i++) {
    tvins.item.pszText = g_GME_Repos_List[i].url;
    if(g_GME_Repos_List[i].enabled) {
      tvins.item.state = INDEXTOSTATEIMAGEMASK(2); // check-box checked image
    } else {
      tvins.item.state = INDEXTOSTATEIMAGEMASK(1); // check-box unchecked image
    }
    SendMessage(htv, TVM_INSERTITEM, 0, (LPARAM)&tvins);
  }

  /* update menus */
  GME_GameUpdMenu();

  return true;
}

bool GME_RepoAddUrl(const char* url)
{
  if(GME_GameGetCurId() == -1) {
    GME_DialogError(g_hwndRepConf, L"Игра не выбрана.");
    return false;
  }

  if(!GME_NetwIsUrl(url)) {
    GME_DialogError(g_hwndRepConf, L"'" + GME_StrToWcs(url) + L"' не похож на корректный URL.");
    return false;
  }

  GME_Repos_Struct repos;
  memset(&repos, 0, sizeof(GME_Repos_Struct));
  strcpy(repos.url, url);
  repos.enabled = true;

  g_GME_Repos_List.push_back(repos);

  if(!GME_RepoWritList()) {
    GME_DialogError(g_hwndRepConf, L"Не удалось записать файл настроек репозиториев.");
    return false;
  }

  GME_RepoUpdList();

  return true;
}

bool GME_RepoRemUrl()
{
  HWND htv = GetDlgItem(g_hwndRepConf, TVS_REPOSLIST);

  char buff[256];
  std::vector<std::string> rem_list;

  HTREEITEM hitem = TreeView_GetSelection(htv);

  TVITEM item;
  memset(&item, 0, sizeof(TVITEM));
  item.mask = TVIF_TEXT|TVIF_STATE;
  item.hItem = hitem;
  item.cchTextMax = 256;
  item.pszText = buff;
  item.stateMask = 0xffffffff;

  //TreeView_GetItem(htv, &item);
  SendMessage(htv, TVM_GETITEM, 0, (LPARAM)&item);
  rem_list.push_back(item.pszText);

  for(unsigned i = 0; i < rem_list.size(); i++) {
    unsigned c = g_GME_Repos_List.size();
    for(unsigned j = 0; j < c; j++) {
      if(rem_list[i] == g_GME_Repos_List[j].url) {
        g_GME_Repos_List.erase(g_GME_Repos_List.begin()+j);
        break;
      }
    }
  }

  if(!GME_RepoWritList()) {
    GME_DialogError(g_hwndRepConf, L"Не удалось записать файл настроек репозиториев.");
    return false;
  }

  GME_RepoUpdList();

  return true;
}



void GME_RepoCheckUpd()
{
  /* empty list view */
  HWND hlv = GetDlgItem(g_hwndRepUpd, LVM_MODSUPDLIST);
  SendMessageW(hlv, LVM_DELETEALLITEMS, 0, 0);

  EnableWindow(GetDlgItem(g_hwndRepUpd, BTN_REPOUPDSEL), false);
  EnableWindow(GetDlgItem(g_hwndRepUpd, BTN_REPOUPDALL), false);

  if(g_GME_ReposMod_List.empty()) {
    GME_DialogWarning(g_hwndRepUpd, L"В репозиториях не найдено данных.");
    return;
  }

  /* get all available local Mod-Archives  */
  std::wstring path;
  std::wstring vers;
  std::vector<std::wstring> name_list;
  std::vector<std::wstring> vers_list;
  std::wstring srch_path = GME_GameGetCurModsPath() + L"\\*";

  WIN32_FIND_DATAW fdw;
  HANDLE hnd = FindFirstFileW(srch_path.c_str(), &fdw);
  if(hnd != INVALID_HANDLE_VALUE) {
    do {
      if(fdw.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        /* update not supported for directory-mod for now

        if(fdw.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
          continue;
        if(!wcscmp(fdw.cFileName, L"."))
          continue;
        if(!wcscmp(fdw.cFileName, L".."))
          continue;
        name = fdw.cFileName;
        name_list.push_back(name);
        type_list.push_back(0);
        */
      } else {
        path = GME_GameGetCurModsPath() + L"\\" + fdw.cFileName;
        if(GME_ZipIsValidMod(path)) {
          name_list.push_back(GME_FilePathToName(fdw.cFileName));
          if(GME_ZipGetModVers(path, &vers)) {
            vers_list.push_back(vers);
          } else {
            vers_list.push_back(L"0.0.0");
          }
        }
      }
    } while(FindNextFileW(hnd, &fdw));
  }
  FindClose(hnd);

  /* add item to list view */
  GME_ModVers_Struct local_version;
  wchar_t wbuff[260];

  LVITEMW lvitm;
  memset(&lvitm, 0, sizeof(LVITEMW));
  lvitm.cchTextMax = 255;

  for(unsigned i = 0; i < g_GME_ReposMod_List.size(); i++) {

    lvitm.mask=LVIF_TEXT|LVIF_IMAGE;
    lvitm.iItem = i;
    lvitm.iSubItem = 0;
    wcscpy(wbuff, g_GME_ReposMod_List[i].name);
    lvitm.pszText = wbuff;
    /* check if mod exists in local */
    lvitm.iImage = 3; /* default image : available */
    for(unsigned k = 0; k < name_list.size(); k++) {
      if(name_list[k] == g_GME_ReposMod_List[i].name) {
        /* check version */
        local_version = GME_RepoParseVers(vers_list[k]);
        if(local_version == g_GME_ReposMod_List[i].version) {
          lvitm.iImage = 2; /* up to date */
        } else {
          if(local_version < g_GME_ReposMod_List[i].version) {
            lvitm.iImage = 1; /* upgrade */
          } else {
            lvitm.iImage = 0; /* downgrade */
          }
        }
        break;
      }
    }
    lvitm.iItem = SendMessageW(hlv, LVM_INSERTITEMW, 0, (LPARAM)&lvitm);

    lvitm.mask=LVIF_TEXT;
    lvitm.iSubItem = 1;
    wcscpy(wbuff, GME_RepoVersString(g_GME_ReposMod_List[i].version).c_str());
    lvitm.pszText = wbuff;
    SendMessageW(hlv, LVM_SETITEMW, 0, (LPARAM)&lvitm);
  }

  EnableWindow(GetDlgItem(g_hwndRepUpd, BTN_REPOUPDSEL), true);
  EnableWindow(GetDlgItem(g_hwndRepUpd, BTN_REPOUPDALL), true);

}

bool GME_RepoChkDesc()
{
  HWND hlv = GetDlgItem(g_hwndRepUpd, LVM_MODSUPDLIST);
  HWND het = GetDlgItem(g_hwndRepUpd, ENT_MODDESC);

  wchar_t name_buff[255];

  /* get list of selected item in the list view control */
  LV_ITEMW lvitm;
  memset(&lvitm, 0, sizeof(LV_ITEMW));
  lvitm.mask = LVIF_TEXT;
  lvitm.cchTextMax = 255;
  lvitm.pszText = name_buff;

  unsigned sel_cnt = 0;
  //unsigned c = SendMessageW(hlv, LVM_GETITEMCOUNT, 0, 0);
  for(unsigned i = 0; i < g_GME_ReposMod_List.size(); i++) {
    if(SendMessageW(hlv, LVM_GETITEMSTATE, i, LVIS_SELECTED)) {

      if(!sel_cnt) {
        if(g_GME_ReposMod_List[i].desc.empty()) {
          SendMessageW(het, WM_SETTEXT, 0, (LPARAM)L"Описание отсутствует.");
        } else {
          SendMessage(het, WM_SETTEXT, 0, (LPARAM)g_GME_ReposMod_List[i].desc.c_str());
        }
        sel_cnt++;
      } else {
        SendMessageW(het, WM_SETTEXT, 0, (LPARAM)L"[Выбрано несколько]");
        return false;
      }
    }
  }

  return true;
}

bool GME_RepoParseXml(const std::wstring& xml, std::vector<GME_ReposMod_Struct>* reposmod_list, std::wstring* log)
{
  GME_ReposMod_Struct reposmod;

  xml_document xmldoc;
  xmldoc.load(xml.c_str(), xml.size());

  // search the first <mod_list> child
  xml_node mod_list;
  for(xml_node child = xmldoc.first_child(); child; child = child.next_sibling()) {
    if(!wcscmp(child.name(), L"mod_list")) {
      mod_list = child;
      break;
    }
  }

  if(mod_list.empty()) {
    GME_DialogWarning(g_hwndRepUpd, L"Ошибка разбора XML репозитория: не найден элемент <mod_list>.");
    GME_Logs(GME_LOG_ERROR, "GME_RepoParseXml", "Parse error", "no <mod_list> child found");
    if(log) *log += L"Ошибка разбора: не найден элемент <mod_list>\r\n";
    return false;
  }

  // get all children of mod_list
  unsigned child_i = 0;
  bool is_unique;
  for(xml_node child = mod_list.first_child(); child; child = child.next_sibling()) {

    if(wcslen(child.name())) {

      if(!child.attribute(L"name")) {
        GME_DialogWarning(g_hwndRepUpd, L"Ошибка разбора XML репозитория: не найден атрибут 'name'.");
        GME_Logs(GME_LOG_ERROR, "GME_RepoParseXml", "Parse error: 'name' attribute not found for child", std::to_string(child_i).c_str());
        if(log) {
           *log += L"Ошибка разбора: не найден атрибут 'name' у элемента #";
           *log += std::to_wstring(child_i);
           *log += L"\r\n";
        }
        continue;
      }
      if(!child.attribute(L"url")) {
        GME_DialogWarning(g_hwndRepUpd, L"Ошибка разбора XML репозитория: не найден атрибут 'url'.");
        GME_Logs(GME_LOG_ERROR, "GME_RepoParseXml", "Parse error: 'url' attribute not found for child", std::to_string(child_i).c_str());
        if(log) {
           *log += L"Ошибка разбора: не найден атрибут 'url' у элемента #";
           *log += std::to_wstring(child_i);
           *log += L"\r\n";
        }
        continue;
      }
      if(!child.attribute(L"version")) {
        GME_DialogWarning(g_hwndRepUpd, L"Ошибка разбора XML репозитория: не найден атрибут 'version'.");
        GME_Logs(GME_LOG_ERROR, "GME_RepoParseXml", "Parse error: 'version' attribute not found for child", std::to_string(child_i).c_str());
        if(log) {
           *log += L"Ошибка разбора: не найден атрибут 'version' у элемента #";
           *log += std::to_wstring(child_i);
           *log += L"\r\n";
        }
        continue;
      }

      reposmod.clear();

      wcscpy(reposmod.name, child.attribute(L"name").value());
      wcstombs(reposmod.url, child.attribute(L"url").value(), wcslen(child.attribute(L"url").value()));
      reposmod.version = GME_RepoParseVers(child.attribute(L"version").value());

      /* check for description */
      if(!child.first_child().empty()) {
        reposmod.desc = GME_ReposXmlDecode(child.child_value()); // this is content between <mod> <mod/>
      } else {
        reposmod.desc.clear();
      }

      /* check if mod is unique */
      is_unique = true;
      for(unsigned i = 0; i < reposmod_list->size(); i++) {
        if(!wcscmp(reposmod.name, reposmod_list->at(i).name)) {
          //reposmod_list->push_back(reposmod);
          is_unique = false;
          if(reposmod.version > reposmod_list->at(i).version) {
            /* replace the old by the new */
            reposmod_list->at(i) = reposmod;
          }
        }
      }
      if(is_unique)
        reposmod_list->push_back(reposmod);
    }
  }

  return true;
}

void GME_RepoDnl_SetItemStatus(const wchar_t* name, const wchar_t* status)
{
  HWND hlv = GetDlgItem(g_hwndRepUpd, LVM_MODSUPDLIST);

  wchar_t buff[255];

  /* get list of selected item in the list view control */
  LV_ITEMW lvitm;
  memset(&lvitm, 0, sizeof(LV_ITEMW));
  lvitm.mask = LVIF_TEXT;
  lvitm.cchTextMax = 255;
  lvitm.pszText = buff;

  unsigned c = SendMessageW(hlv, LVM_GETITEMCOUNT, 0, 0);
  for(unsigned i = 0; i < c; i++) {
    lvitm.iItem = i;
    SendMessageW(hlv ,LVM_GETITEMW, 0, (LPARAM)&lvitm);
    if(!wcscmp(lvitm.pszText, name)) {
      lvitm.iSubItem = 2; // status
      wcscpy(buff, status);
      lvitm.pszText = buff;
      SendMessageW(hlv, LVM_SETITEMW, 0, (LPARAM)&lvitm);
    }
  }
}

void GME_RepoDnl_SetItemProgress(const wchar_t* name, int percent)
{
  HWND hlv = GetDlgItem(g_hwndRepUpd, LVM_MODSUPDLIST);

  wchar_t buff[255];

  /* get list of selected item in the list view control */
  LV_ITEMW lvitm;
  memset(&lvitm, 0, sizeof(LV_ITEMW));
  lvitm.mask = LVIF_TEXT;
  lvitm.cchTextMax = 255;
  lvitm.pszText = buff;

  unsigned c = SendMessageW(hlv, LVM_GETITEMCOUNT, 0, 0);
  for(unsigned i = 0; i < c; i++) {
    lvitm.iItem = i;
    SendMessageW(hlv ,LVM_GETITEMW, 0, (LPARAM)&lvitm);
    if(!wcscmp(lvitm.pszText, name)) {
      lvitm.iSubItem = 3; // progress
      if(percent < 0) {
        buff[0] = 0;
      } else {
        swprintf(buff, L"%d %%", percent);
      }
      lvitm.pszText = buff;
      SendMessageW(hlv, LVM_SETITEMW, 0, (LPARAM)&lvitm);
    }
  }
}

void GME_RepoDnl_OnErr(const char* url)
{
  HWND hpb = GetDlgItem(g_hwndRepUpd, PBM_DONWLOAD);
  SendMessage(hpb, PBM_SETPOS, (WPARAM)0, 0);
  SetDlgItemTextW(g_hwndRepUpd, TXT_DOWNSPEED, L"---- КиБ/с");
  GME_RepoDnl_SetItemProgress(g_GME_ReposDnl_List[g_ReposQry_Id].name, -1);
}

bool GME_RepoDnl_OnDnl(unsigned percent, unsigned rate)
{
  HWND hpb = GetDlgItem(g_hwndRepUpd, PBM_DONWLOAD);
  SendMessage(hpb, PBM_SETPOS, (WPARAM)percent, 0);
  /* update item in list view */
  wchar_t buff[64];
  swprintf(buff, 64, L"%d КиБ/с", (rate/1024));
  SetDlgItemTextW(g_hwndRepUpd, TXT_DOWNSPEED, buff);
  GME_RepoDnl_SetItemProgress(g_GME_ReposDnl_List[g_ReposQry_Id].name, percent);
  return (g_ReposQry_Cancel == false);
}

void GME_RepoDnl_OnSav(const wchar_t* path)
{
  HWND hpb = GetDlgItem(g_hwndRepUpd, PBM_DONWLOAD);
  SendMessage(hpb, PBM_SETPOS, (WPARAM)100, 0);
  SetDlgItemTextW(g_hwndRepUpd, TXT_DOWNSPEED, L"---- КиБ/с");
  /* delete old file */
  std::wstring mod_path = GME_GameGetCurModsPath() + L"\\";
  mod_path += g_GME_ReposDnl_List[g_ReposQry_Id].name;
  mod_path += L".zip";
  if(GME_IsFile(mod_path)) {
    if(!GME_FileDelete(mod_path)) {
      GME_Logs(GME_LOG_ERROR, "GME_RepoDnl_OnSav", "Unable to delete old file", GME_StrToMbs(mod_path).c_str());
      GME_DialogWarning(g_hwndRepUpd, L"Ошибка завершения загрузки, подробности в журнале отладки.");
    }
  }
  if(!GME_FileMove(path, mod_path, true)) {
    GME_Logs(GME_LOG_ERROR, "GME_RepoDnl_OnSav", "Unable to rename temporary file", GME_StrToMbs(path).c_str());
    GME_DialogWarning(g_hwndRepUpd, L"Ошибка завершения загрузки, подробности в журнале отладки.");
  }
  /* update item in list view */
  GME_RepoDnl_SetItemProgress(g_GME_ReposDnl_List[g_ReposQry_Id].name, -1);
  GME_RepoDnl_SetItemStatus(g_GME_ReposDnl_List[g_ReposQry_Id].name, L"Готово");
  GME_Logs(GME_LOG_NOTICE, "GME_RepoDnl_OnSav", "Download Done", GME_StrToMbs(mod_path).c_str());
}

DWORD WINAPI GME_RepoQueryDnl_Th(void* args)
{
  GME_Logs(GME_LOG_NOTICE, "GME_RepoQueryDnl_Th", "Download process", "...");

  g_ReposQry_Cancel = false;
  g_ReposQry_Running = true;

  HWND hpb = GetDlgItem(g_hwndRepUpd, PBM_DONWLOAD);

  int http_err;
  int http_fail = 0;
  std::string err_msg;
  std::wstring err_wmsg;

  for(unsigned i = 0; i < g_GME_ReposDnl_List.size(); i++) {

    g_ReposQry_Id = i;

    /* disabling old version before download */
    GME_RepoDnl_SetItemStatus(g_GME_ReposDnl_List[i].name, L"Отключение старого...");
    GME_ModsRestoreMod(hpb, g_GME_ReposDnl_List[i].name);

    /* here we go for file download */
    SendMessage(hpb, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    EnableWindow(GetDlgItem(g_hwndRepUpd, BTN_CANCEL), true);
    GME_RepoDnl_SetItemStatus(g_GME_ReposDnl_List[i].name, L"Загрузка...");
    GME_Logs(GME_LOG_NOTICE, "GME_RepoQueryDnl_Th", "Downloading", g_GME_ReposDnl_List[i].url);
    http_err = GME_NetwHttpGET(g_GME_ReposDnl_List[i].url, GME_RepoDnl_OnErr, GME_RepoDnl_OnDnl, GME_RepoDnl_OnSav, GME_GameGetCurModsPath());
    if(http_err) {
      http_fail++;
      if(http_err < 10) {
        err_wmsg = L"Не удалось загрузить '" + std::wstring(g_GME_ReposDnl_List[i].name) + L"':\r\n\r\n    ";
        switch(http_err)
        {
        case GME_HTTPGET_ERR_DNS:
          GME_RepoDnl_SetItemStatus(g_GME_ReposDnl_List[i].name, L"Узел не найден");
          GME_Logs(GME_LOG_WARNING, "GME_RepoQueryDnl_Th", "Download failed", "Host not found");
          err_wmsg += L"Узел не найден";
          break;
        case GME_HTTPGET_ERR_CNX:
          GME_RepoDnl_SetItemStatus(g_GME_ReposDnl_List[i].name, L"Ошибка соединения");
          GME_Logs(GME_LOG_WARNING, "GME_RepoQueryDnl_Th", "Download failed", "Connection error");
          err_wmsg += L"Ошибка соединения";
          break;
        case GME_HTTPGET_ERR_ENC:
          GME_RepoDnl_SetItemStatus(g_GME_ReposDnl_List[i].name, L"Ошибка передачи HTTP");
          GME_Logs(GME_LOG_WARNING, "GME_RepoQueryDnl_Th", "Download failed", "HTTP transfer error");
          err_wmsg += L"Ошибка передачи HTTP";
          break;
        case GME_HTTPGET_ERR_REC:
          GME_RepoDnl_SetItemStatus(g_GME_ReposDnl_List[i].name, L"Соединение потеряно");
          GME_Logs(GME_LOG_WARNING, "GME_RepoQueryDnl_Th", "Download failed", "Connection lost");
          err_wmsg += L"Соединение потеряно";
          break;
        case GME_HTTPGET_ERR_FOP:
          GME_RepoDnl_SetItemStatus(g_GME_ReposDnl_List[i].name, L"Ошибка открытия файла");
          GME_Logs(GME_LOG_WARNING, "GME_RepoQueryDnl_Th", "Download failed", "I/O Open error");
          err_wmsg += L"Ошибка открытия файла";
          break;
        case GME_HTTPGET_ERR_FWR:
          GME_RepoDnl_SetItemStatus(g_GME_ReposDnl_List[i].name, L"Ошибка записи файла");
          GME_Logs(GME_LOG_WARNING, "GME_RepoQueryDnl_Th", "Download failed", "I/O Write error");
          err_wmsg += L"Ошибка записи файла";
          break;
        }
        GME_DialogWarning(g_hwndRepUpd, err_wmsg);
      } else {
        err_msg = "HTTP error ";
        err_msg += std::to_string(http_err);
        err_wmsg = L"Ошибка HTTP " + std::to_wstring(http_err);
        GME_RepoDnl_SetItemStatus(g_GME_ReposDnl_List[i].name, err_wmsg.c_str());
        GME_Logs(GME_LOG_WARNING, "GME_RepoQueryDnl_Th", "Download failed", err_msg.c_str());
        GME_DialogWarning(g_hwndRepUpd, L"Не удалось загрузить '" + std::wstring(g_GME_ReposDnl_List[i].name) + L"':\r\n\r\n    " + err_wmsg);
      }
    }

    if(g_ReposQry_Cancel) {
      SendMessage(hpb, PBM_SETPOS, (WPARAM)0, 0);
      SetDlgItemTextW(g_hwndRepUpd, TXT_DOWNSPEED, L"---- КиБ/с");

      for(unsigned i = 0; i < g_GME_ReposDnl_List.size(); i++) {
        GME_RepoDnl_SetItemStatus(g_GME_ReposDnl_List[i].name, L"");
        GME_RepoDnl_SetItemProgress(g_GME_ReposDnl_List[i].name, -1);
      }
      g_GME_ReposDnl_List.clear();

      EnableWindow(GetDlgItem(g_hwndRepUpd, BTN_CANCEL), false);

      g_ReposQry_Running = false;

      GME_Logs(GME_LOG_WARNING, "GME_RepoQueryDnl_Th", "Download process", "Aborted by user");

      return 0;
    }

  }

  SendMessage(hpb, PBM_SETPOS, (WPARAM)0, 0);
  SetDlgItemTextW(g_hwndRepUpd, TXT_DOWNSPEED, L"---- КиБ/с");

  g_GME_ReposDnl_List.clear();

  GME_RepoCheckUpd();

  GME_ModsUpdList();

  g_ReposQry_Running = false;

  GME_Logs(GME_LOG_NOTICE, "GME_RepoQueryDnl_Th", "Download process", "Done");

  return 0;
}


void GME_RepoUpd_OnErr(const char* url)
{
  HWND hpb = GetDlgItem(g_hwndRepUpd, PBM_REPOQRY);
  SendMessage(hpb, PBM_SETPOS, (WPARAM)0, 0);
}

bool GME_RepoUpd_OnDnl(unsigned percent, unsigned rate)
{
  HWND hpb = GetDlgItem(g_hwndRepUpd, PBM_REPOQRY);
  SendMessage(hpb, PBM_SETPOS, (WPARAM)percent, 0);
  return !g_ReposQry_Cancel;
}

void GME_RepoUpd_OnEnd(const char* body, size_t body_size)
{
  HWND hpb = GetDlgItem(g_hwndRepUpd, PBM_REPOQRY);
  SendMessage(hpb, PBM_SETPOS, (WPARAM)100, 0);
  //GME_RepoParseXml(GME_StrToWcs(body));
  if(!GME_RepoParseXml(GME_StrToWcs(body), &g_GME_ReposMod_List, NULL)) {
    GME_DialogWarning(g_hwndRepUpd, L"Ошибка запроса к репозиторию: ошибка разбора XML.");
  }
}

DWORD WINAPI GME_RepoQueryUpd_Th(void* args)
{
  GME_Logs(GME_LOG_NOTICE, "GME_RepoQueryUpd_Th", "Query repositories", "...");

  g_ReposQry_Running = true;
  g_ReposQry_Cancel = false;

  GME_RepoUpdList();

  if(g_GME_Repos_List.empty()) {
    GME_DialogWarning(g_hwndRepUpd, L"Список репозиториев пуст.");
    g_ReposQry_Running = false;
    return 0;
  }

  SetDlgItemTextW(g_hwndRepUpd, TXT_REPOQRYSTATUS, L"Получение данных из репозиториев...");
  HWND hpb = GetDlgItem(g_hwndRepUpd, PBM_REPOQRY);

  int http_err;
  int http_fail = 0;
  int http_reqs = 0;
  std::string err_msg;
  std::wstring err_wmsg;
  std::string xml_url;
  std::wstring cnx_msg;

  for(unsigned i = 0; i < g_GME_Repos_List.size(); i++) {

    if(!g_GME_Repos_List[i].enabled)
      continue;

    http_reqs++;

    g_ReposQry_Id = i;

    xml_url.clear();
    xml_url = g_GME_Repos_List[i].url;

    /* check for ".php" or ".asp(x)" for dynamic content */
    bool dyn_content = false;
    if(xml_url.find("?", 0) != std::string::npos) {
      dyn_content = true;
    }
    if(xml_url.find(".php", xml_url.size()-5) != std::string::npos) {
      dyn_content = true;
    }
    if(xml_url.find(".asp", xml_url.size()-5) != std::string::npos) {
      dyn_content = true;
    }
    if(xml_url.find(".aspx", xml_url.size()-5) != std::string::npos) {
      dyn_content = true;
    }

    /* check for ".xml" at end of URL, if not, we add it */
    if(!dyn_content) {
      if(xml_url.find(".xml", xml_url.size()-5) == std::string::npos) {
        xml_url += ".xml";
      }
    }

    SendMessage(hpb, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    unsigned l = std::string(g_GME_Repos_List[i].url).find_first_of("/", 0);
    cnx_msg = L"Подключение к " + GME_StrToWcs(std::string(g_GME_Repos_List[i].url).substr(0, l)) + L", подождите...";
    SetDlgItemTextW(g_hwndRepUpd, TXT_REPOQRYURL, cnx_msg.c_str());

    GME_Logs(GME_LOG_NOTICE, "GME_RepoQueryUpd_Th", "Try HTTP access", xml_url.c_str());

    http_err = GME_NetwHttpGET(xml_url.c_str(), GME_RepoUpd_OnErr, GME_RepoUpd_OnDnl, GME_RepoUpd_OnEnd);
    if(http_err) {
      err_msg = "Query failed for repository '";
      err_msg += xml_url;
      err_msg += "':\r\n\r\n    ";
      err_wmsg = L"Ошибка запроса к репозиторию '" + GME_StrToWcs(xml_url) + L"':\r\n\r\n    ";
      http_fail++;
      if(http_err < 10) {
        switch(http_err)
        {
        case GME_HTTPGET_ERR_DNS:
          err_msg += "Connection error: Host not found.";
          err_wmsg += L"Ошибка соединения: узел не найден.";
          GME_Logs(GME_LOG_WARNING, "GME_RepoQueryUpd_Th", "Connection failed", "Host not found");
          break;
        case GME_HTTPGET_ERR_CNX:
          err_msg += "Connection error: Connection refused or timed out.";
          err_wmsg += L"Ошибка соединения: соединение отклонено или истекло время ожидания.";
          GME_Logs(GME_LOG_WARNING, "GME_RepoQueryUpd_Th", "Connection failed", "Connection refused or timed out");
          break;
        case GME_HTTPGET_ERR_ENC:
          err_msg += "Unsupported HTTP chunked transfer encoding.";
          err_wmsg += L"Неподдерживаемая кодировка передачи HTTP (chunked).";
          GME_Logs(GME_LOG_WARNING, "GME_RepoQueryUpd_Th", "Connection failed", "Unsupported HTTP chunked transfer encoding");
          break;
        case GME_HTTPGET_ERR_BAL:
          err_msg += "Bad alloc, no enough memory to store HTTP response.";
          err_wmsg += L"Недостаточно памяти для хранения ответа HTTP.";
          GME_Logs(GME_LOG_WARNING, "GME_RepoQueryUpd_Th", "Connection failed", "Bad alloc, no enough memory to store HTTP response");
          break;
        case GME_HTTPGET_ERR_REC:
          err_msg += "Connection error: Lost connection.";
          err_wmsg += L"Ошибка соединения: соединение потеряно.";
          GME_Logs(GME_LOG_WARNING, "GME_RepoQueryUpd_Th", "Connection failed", "Lost connection");
          break;
        }
      } else {
        err_msg += "HTTP request error: ";
        err_msg += std::to_string(http_err);
        err_wmsg += L"Ошибка HTTP-запроса: " + std::to_wstring(http_err);
        switch(http_err)
        {
        case 401: err_msg += " (Unauthorized)"; err_wmsg += L" (Требуется авторизация)"; break;
        case 400: err_msg += " (Bad request)"; err_wmsg += L" (Неверный запрос)"; break;
        case 403: err_msg += " (Forbidden)"; err_wmsg += L" (Доступ запрещён)"; break;
        case 404: err_msg += " (Not found)"; err_wmsg += L" (Не найдено)"; break;
        case 500: err_msg += " (Internal server error)"; err_wmsg += L" (Внутренняя ошибка сервера)"; break;
        case 502: err_msg += " (Bad gateway)"; err_wmsg += L" (Ошибочный шлюз)"; break;
        case 503: err_msg += " (Service temporarily unavailable)"; err_wmsg += L" (Сервис временно недоступен)"; break;
        case 504: err_msg += " (Gateway time-out)"; err_wmsg += L" (Шлюз не отвечает)"; break;
        case 408: err_msg += " (Request Time-Out)"; err_wmsg += L" (Истекло время запроса)"; break;
        case 410: err_msg += " (Gone)"; err_wmsg += L" (Ресурс удалён)"; break;
        }
      }
      GME_Logs(GME_LOG_WARNING, "GME_RepoQueryUpd_Th", "HTTP access failed", err_msg.c_str());
      GME_DialogWarning(g_hwndRepUpd, err_wmsg);
    }

    if(g_ReposQry_Cancel) {
      g_ReposQry_Running = false;
      return 0;
    }
  }

  SendMessage(hpb, PBM_SETPOS, (WPARAM)0, 0);
  SetDlgItemTextW(g_hwndRepUpd, TXT_REPOQRYSTATUS, L"Опрос репозиториев завершён");
  wchar_t result[64];
  swprintf(result, 64, L"Проверено репозиториев: %d, с ошибкой: %d.", http_reqs, http_fail);
  SetDlgItemTextW(g_hwndRepUpd, TXT_REPOQRYURL, result);

  GME_RepoCheckUpd();

  g_ReposQry_Running = false;

  return 0;
}


/*
  function to query all repositories for updates
*/
void GME_RepoQueryUpd()
{
  g_GME_ReposMod_List.clear();
  g_ReposQry_hT = CreateThread(NULL,0,GME_RepoQueryUpd_Th,NULL,0,&g_ReposQry_iT);
}


void GME_RepoDownloadSel()
{
  if(!g_GME_ReposDnl_List.empty()) {
    GME_DialogWarning(g_hwndRepUpd, L"Загрузка уже выполняется. Подождите или отмените текущий процесс...");
    return;
  }

  HWND hlv = GetDlgItem(g_hwndRepUpd, LVM_MODSUPDLIST);

  std::vector<std::wstring> reject_list;
  wchar_t name_buff[255];

  /* get list of selected item in the list view control */
  LV_ITEMW lvitm;
  memset(&lvitm, 0, sizeof(LV_ITEMW));
  lvitm.mask = LVIF_TEXT|LVIF_IMAGE;
  lvitm.cchTextMax = 255;
  lvitm.pszText = name_buff;

  unsigned c = SendMessageW(hlv, LVM_GETITEMCOUNT, 0, 0);
  for(unsigned i = 0; i < c; i++) {
    if(SendMessageW(hlv, LVM_GETITEMSTATE, i, LVIS_SELECTED)) {
      lvitm.iItem = i;
      SendMessageW(hlv ,LVM_GETITEMW, 0, (LPARAM)&lvitm);
      if(lvitm.iImage == 2) {
        /* already up to date */
        reject_list.push_back(lvitm.pszText);
      } else {
        for(unsigned j = 0; j < g_GME_ReposMod_List.size(); j++) {
          if(!wcscmp(lvitm.pszText, g_GME_ReposMod_List[j].name)) {
            g_GME_ReposDnl_List.push_back(g_GME_ReposMod_List[j]);
            GME_RepoDnl_SetItemStatus(g_GME_ReposMod_List[j].name, L"В очереди...");
            break;
          }
        }
      }
    }
  }

  if(!reject_list.empty()) {
    std::wstring msg = L"Уже актуальные моды загружены не будут.\n\n";
    for(unsigned i = 0; i < reject_list.size(); i++) {
      msg += reject_list[i] + L"\n";
    }
    GME_DialogInfo(g_hwndRepUpd, msg);
  }

  g_ReposQry_hT = CreateThread(NULL,0,GME_RepoQueryDnl_Th,NULL,0,&g_ReposQry_iT);
}


void GME_RepoDownloadAll()
{
  if(!g_GME_ReposDnl_List.empty()) {
    GME_DialogWarning(g_hwndRepUpd, L"Загрузка уже выполняется. Подождите или отмените текущий процесс...");
    return;
  }

  HWND hlv = GetDlgItem(g_hwndRepUpd, LVM_MODSUPDLIST);

  std::vector<std::wstring> reject_list;
  wchar_t name_buff[255];

  /* get list of selected item in the list view control */
  LV_ITEMW lvitm;
  memset(&lvitm, 0, sizeof(LV_ITEMW));
  lvitm.mask = LVIF_TEXT|LVIF_IMAGE;
  lvitm.cchTextMax = 255;
  lvitm.pszText = name_buff;

  unsigned c = SendMessageW(hlv, LVM_GETITEMCOUNT, 0, 0);
  for(unsigned i = 0; i < c; i++) {
    lvitm.iItem = i;
    SendMessageW(hlv ,LVM_GETITEMW, 0, (LPARAM)&lvitm);
    if(lvitm.iImage == 2) {
      /* already up to date */
      reject_list.push_back(lvitm.pszText);
    } else {
      for(unsigned j = 0; j < g_GME_ReposMod_List.size(); j++) {
        if(!wcscmp(lvitm.pszText, g_GME_ReposMod_List[j].name)) {
          g_GME_ReposDnl_List.push_back(g_GME_ReposMod_List[j]);
          GME_RepoDnl_SetItemStatus(g_GME_ReposMod_List[j].name, L"В очереди...");
          break;
        }
      }
    }
  }

  if(!reject_list.empty()) {
    std::wstring msg = L"Уже актуальные моды загружены не будут.\n\n";
    for(unsigned i = 0; i < reject_list.size(); i++) {
      msg += reject_list[i] + L"\n";
    }
    GME_DialogInfo(g_hwndRepUpd, msg);
  }

  g_ReposQry_hT = CreateThread(NULL,0,GME_RepoQueryDnl_Th,NULL,0,&g_ReposQry_iT);
}


/*
  function to cancel all current requests or download
*/
void GME_RepoQueryCancel()
{
  if(g_ReposQry_Running) {
    g_ReposQry_Cancel = true;
  }
}

/*
  function to replace CR and LF by valid XML codes
*/
std::string GME_ReposXmlEncode(const std::wstring& src)
{
  std::string source;
  std::string encode;
  GME_StrToMbs(source, src);

  for(unsigned i = 0; i < source.size(); i++) {
    if(source[i] == '\r' || source[i] == '\n' || source[i] == '&') {
      encode.append("&#");
      if(source[i] == '\r') {
        encode.append("13;");
      }
      if(source[i] == '\n') {
        encode.append("10;");
      }
      if(source[i] == '&') {
        encode.append("26;");
      }
    } else {
      encode.append(1, source[i]);
    }
  }
  return encode;
}

std::string GME_ReposXmlDecode(const std::wstring& src)
{
  std::string source;
  std::string decode;
  GME_StrToMbs(source, src);

  char ccode[16];
  unsigned icode;
  unsigned n;

  for(unsigned i = 0; i < source.size(); i++) {
    if(source[i] == '&') {
      i+=2; // #
      for(n = 0; source[i] != ';'; n++, i++) {
        ccode[n] = source[i];
      }
      ccode[n] = '\0';
      icode = strtol(ccode, NULL, 10);
      if(icode == 13) { decode.append(1, '\r'); continue; }
      if(icode == 10) { decode.append(1, '\n'); continue; }
      if(icode == 26) { decode.append(1, '&'); continue; }
    } else {
      decode.append(1, source[i]);
    }
  }

  return decode;
}

std::string GME_RepoMakeXml(const char* url_str, bool cust_path, const wchar_t* path_str)
{

  if(!GME_NetwIsUrl(url_str)) {
    GME_DialogError(g_hwndRepXml, L"'" + GME_StrToWcs(url_str) + L"' не похож на корректный URL.");
    return std::string();
  }

  std::string base_url = url_str;
  if(base_url[base_url.size()-1] != '/') {
    base_url.push_back('/');
  }

  /* get all available ZIP mod list */
  std::wstring path;
  std::wstring vers;
  std::wstring desc;
  std::vector<std::wstring> name_list;
  std::vector<std::wstring> vers_list;
  std::vector<std::wstring> desc_list;
  std::wstring srch_path;
  if(cust_path) {
    srch_path = path_str;
    srch_path += L"\\*.zip";
  } else {
    srch_path = GME_GameGetCurModsPath() + L"\\*.zip";
  }

  WIN32_FIND_DATAW fdw;
  HANDLE hnd = FindFirstFileW(srch_path.c_str(), &fdw);
  if(hnd != INVALID_HANDLE_VALUE) {
    do {
      if(!(fdw.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        path = GME_GameGetCurModsPath() + L"\\" + fdw.cFileName;
        if(GME_ZipIsValidMod(path)) {
          name_list.push_back(GME_FilePathToName(fdw.cFileName));
          if(GME_ZipGetModVers(path, &vers)) {
            vers_list.push_back(vers);
          } else {
            vers_list.push_back(L"0.0.0");
          }
          GME_ZipGetModDesc(path, &desc);
          desc_list.push_back(desc);
        }
      }
    } while(FindNextFileW(hnd, &fdw));
  }
  FindClose(hnd);

  if(name_list.empty()) {
    GME_DialogWarning(g_hwndRepXml, L"Не найдено корректных мод-архивов для создания XML.");
    return std::string();
  }

  std::string xml_ascii;
  xml_ascii = "<?xml version=\"1.0\"?>\r\n";
  xml_ascii += "<mod_list>\r\n";

  for(unsigned i = 0; i < name_list.size(); i++) {
    xml_ascii += "  <mod name=\"";
    xml_ascii += GME_StrToMbs(name_list[i]);
    xml_ascii += "\" version=\"";
    xml_ascii += GME_StrToMbs(vers_list[i]);
    xml_ascii += "\" url=\"";
    xml_ascii += GME_NetwEncodeUrl(base_url + GME_StrToMbs(name_list[i]));
    xml_ascii += ".zip\">";
    if(!desc_list[i].empty()) {
      //xml_ascii += "    ";
      xml_ascii += GME_ReposXmlEncode(desc_list[i]);
      //xml_ascii += "\r\n";
    }
    xml_ascii += "</mod>\r\n";
  }
  xml_ascii += "</mod_list>\r\n";

  SetDlgItemText(g_hwndRepXml, ENT_OUTPUT, xml_ascii.c_str());

  return xml_ascii;
}


bool GME_RepoSaveXml()
{
  char* xml_src = NULL;
  size_t xml_src_size;

  xml_src_size = GetWindowTextLength(GetDlgItem(g_hwndRepXml, ENT_OUTPUT));

  if(xml_src_size < 2) {
    GME_DialogWarning(g_hwndRepXml, L"Буфер XML-источника пуст.");
    return false;
  }

  unsigned path_offset;
  wchar_t buffer[260];

  if(!GME_DialogFileSave(g_hwndRepXml, buffer, 260, &path_offset, L"xml", L"Файл XML (*.xml)\0*.XML;\0", L"")) {
    return false;
  }

  std::wstring file_path = buffer;

  if(GME_IsFile(file_path)) {
    if(IDNO == GME_DialogQuestionConfirm(g_hwndRepXml, L"Файл '" + file_path + L"' уже существует. Перезаписать?")) {
      return false;
    }
  }

  FILE* fp = _wfopen(file_path.c_str(), L"wb");
  if(fp) {

    try {
      xml_src = new char[xml_src_size+2];
    } catch(const std::bad_alloc&) {
      GME_Logs(GME_LOG_ERROR, "GME_RepoSaveXml", "Bad alloc", std::to_string(xml_src_size+2).c_str());
      fclose(fp);
      return false;
    }
    if(xml_src == NULL) {
      GME_Logs(GME_LOG_ERROR, "GME_RepoSaveXml", "Bad alloc (* == NULL)", std::to_string(xml_src_size+2).c_str());
      fclose(fp);
      return false;
    }

    GetDlgItemText(g_hwndRepXml, ENT_OUTPUT, (char*)xml_src, xml_src_size+1);

    if(fwrite(xml_src, 1, xml_src_size, fp) != xml_src_size) {
      fclose(fp);
      delete[] xml_src;
      GME_Logs(GME_LOG_ERROR, "GME_RepoSaveXml", "Write error", GME_StrToMbs(file_path).c_str());
      return false;
    }

    fclose(fp);
    delete[] xml_src;
    GME_DialogInfo(g_hwndRepXml, L"XML-файл репозитория '" + file_path + L"' успешно сохранён.");
    return true;
  }

  GME_DialogWarning(g_hwndRepXml, L"Не удалось записать файл '" + file_path + L"'.");

  return false;
}

static std::wstring GME_RepoAcpToWcs(const std::string& str)
{
  // ANSI code page, same conversion the former SetDlgItemTextA applied
  int n = MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, NULL, 0);
  if(n <= 0) return std::wstring();
  std::wstring wcs(n, L'\0');
  MultiByteToWideChar(CP_ACP, 0, str.c_str(), -1, &wcs[0], n);
  wcs.resize(n - 1);
  return wcs;
}

bool GME_RepoTestXml(const wchar_t* path, unsigned offst)
{
  std::wstring log;
  std::wstring output;
  std::wstring content;
  std::wstring file_path = path;

  if(GME_FileGetAsciiContent(file_path, &content) > 0) {

    std::vector<GME_ReposMod_Struct> reposmod_list;

    if(!GME_RepoParseXml(content, &reposmod_list, &log)) {
      SetDlgItemTextW(g_hwndRepXts, TXT_MESSAGE, L"Ошибка разбора XML-файла репозитория: XML-источник некорректен.");
      SetDlgItemTextW(g_hwndRepXts, ENT_OUTPUT, log.c_str());
      return false;
    }

    SetDlgItemTextW(g_hwndRepXts, TXT_MESSAGE, L"XML-файл репозитория успешно разобран: XML-источник корректен.");

    for(unsigned i = 0; i < reposmod_list.size(); i++) {
      output += L"==================================================================================================\r\nМод: \"";
      output += reposmod_list[i].name;
      output += L"\"\r\nВерсия: \"";
      output += GME_RepoVersString(reposmod_list[i].version);
      output += L"\"\r\nURL: \"";
      output += GME_RepoAcpToWcs(reposmod_list[i].url);
      output += L"\"\r\nОписание:\r\n";
      output += reposmod_list[i].desc.empty() ? std::wstring(L"Описание отсутствует.") : GME_RepoAcpToWcs(reposmod_list[i].desc);
      output += L"\r\n\r\n";
    }

    SetDlgItemTextW(g_hwndRepXts, ENT_OUTPUT, output.c_str());

  } else {
    GME_DialogWarning(g_hwndRepXml, L"Не удалось прочитать файл '" + file_path + L"'.");
    return false;
  }

  return true;
}
