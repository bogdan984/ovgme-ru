; Установщик OvGME (русская версия). Сборка: makensis installer/ovgme.nsi
Unicode true
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "x64.nsh"

!define APPNAME "OvGME"
!define APPVER  "1.7.4"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APPNAME}"

Name "${APPNAME} ${APPVER}"
OutFile "..\dist\OvGME_RU_${APPVER}_setup.exe"
InstallDir "$PROGRAMFILES64\${APPNAME}"
InstallDirRegKey HKLM "${UNINSTKEY}" "InstallLocation"
RequestExecutionLevel admin
BrandingText "${APPNAME} ${APPVER}"

VIProductVersion "${APPVER}.0"
VIAddVersionKey /LANG=1049 "ProductName" "${APPNAME}"
VIAddVersionKey /LANG=1049 "FileDescription" "Установка ${APPNAME}"
VIAddVersionKey /LANG=1049 "FileVersion" "${APPVER}"
VIAddVersionKey /LANG=1049 "LegalCopyright" "GNU GPL v3"

!define MUI_ICON "..\res\icon.ico"
!define MUI_UNICON "..\res\icon.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\OvGME.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Запустить OvGME"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\gpl-3.0.txt"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "Russian"

; главное окно OvGME — диалог (#32770) с заголовком "OvGME"
!macro WAIT_OVGME_CLOSED
  check:
    FindWindow $0 "#32770" "OvGME"
    StrCmp $0 0 done
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "OvGME сейчас запущена. Закройте программу и повторите попытку." IDRETRY check
    Abort
  done:
!macroend

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "Эта версия OvGME работает только в 64-битной Windows."
    Abort
  ${EndIf}
  !insertmacro WAIT_OVGME_CLOSED
  SetRegView 64
  SetShellVarContext all
FunctionEnd

Function un.onInit
  !insertmacro WAIT_OVGME_CLOSED
  SetRegView 64
  SetShellVarContext all
FunctionEnd

Section "OvGME (обязательно)" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "..\bin\OvGME.exe"
  File "..\bin\ovgme.chm"
  File "..\README.txt"
  File "..\gpl-3.0.txt"
  File "..\CHANGELOG.txt"

  WriteUninstaller "$INSTDIR\uninstall.exe"

  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\OvGME.lnk" "$INSTDIR\OvGME.exe"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\Справка OvGME.lnk" "$INSTDIR\ovgme.chm"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\Удалить OvGME.lnk" "$INSTDIR\uninstall.exe"

  WriteRegStr HKLM "${UNINSTKEY}" "DisplayName" "${APPNAME} ${APPVER} (русская версия)"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayVersion" "${APPVER}"
  WriteRegStr HKLM "${UNINSTKEY}" "Publisher" "OvGME"
  WriteRegStr HKLM "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\OvGME.exe"
  WriteRegStr HKLM "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTKEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTKEY}" "NoRepair" 1
  WriteRegDWORD HKLM "${UNINSTKEY}" "EstimatedSize" 2000
SectionEnd

Section "Ярлык на столе" SecDesktop
  CreateShortcut "$DESKTOP\OvGME.lnk" "$INSTDIR\OvGME.exe"
SectionEnd

LangString DESC_SecMain ${LANG_RUSSIAN} "Программа OvGME и русская справка."
LangString DESC_SecDesktop ${LANG_RUSSIAN} "Создать ярлык OvGME на рабочем столе."
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} $(DESC_SecMain)
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_SecDesktop)
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  ; как в оригинале: OvGME сам отключает все моды и восстанавливает файлы игр
  ExecWait '"$INSTDIR\OvGME.exe" /uninstall'

  Delete "$INSTDIR\OvGME.exe"
  Delete "$INSTDIR\ovgme.chm"
  Delete "$INSTDIR\README.txt"
  Delete "$INSTDIR\gpl-3.0.txt"
  Delete "$INSTDIR\CHANGELOG.txt"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\${APPNAME}\OvGME.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\Справка OvGME.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\Удалить OvGME.lnk"
  RMDir "$SMPROGRAMS\${APPNAME}"
  Delete "$DESKTOP\OvGME.lnk"

  DeleteRegKey HKLM "${UNINSTKEY}"
SectionEnd
