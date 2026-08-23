Unicode True

!include "MUI2.nsh"
!include "x64.nsh"
!include "LogicLib.nsh"

!ifndef AOA_STAGE_DIR
  !error "Pass /DAOA_STAGE_DIR=... (staged Release files)."
!endif
!ifndef AOA_OUT_FILE
  !error "Pass /DAOA_OUT_FILE=... (output setup exe)."
!endif
!ifndef AOA_LICENSE_FILE
  !error "Pass /DAOA_LICENSE_FILE=... (EULA text)."
!endif
!ifndef AOA_DISPLAY_VERSION
  !define AOA_DISPLAY_VERSION "alpha_v0.2.1"
!endif
!ifndef AOA_PRODUCT_VERSION
  !define AOA_PRODUCT_VERSION "0.2.1.0"
!endif

!define PRODUCT_NAME "Age of Affinities"
!define COMPANY_NAME "HighTeam"
!define PRODUCT_PUBLISHER "HighTeam"
!define UNINSTALL_REG "Software\Microsoft\Windows\CurrentVersion\Uninstall\AgeOfAffinities"

Name "${PRODUCT_NAME}"
OutFile "${AOA_OUT_FILE}"
InstallDir "$PROGRAMFILES64\${COMPANY_NAME}\${PRODUCT_NAME}"
InstallDirRegKey HKLM "${UNINSTALL_REG}" "InstallLocation"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show
BrandingText "${COMPANY_NAME}"

VIProductVersion "${AOA_PRODUCT_VERSION}"
VIAddVersionKey "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey "CompanyName" "${COMPANY_NAME}"
VIAddVersionKey "FileDescription" "${PRODUCT_NAME} Setup"
VIAddVersionKey "FileVersion" "${AOA_DISPLAY_VERSION}"
VIAddVersionKey "ProductVersion" "${AOA_DISPLAY_VERSION}"
VIAddVersionKey "LegalCopyright" "Copyright (c) 2026 ${COMPANY_NAME}"

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\AgeofAffinities.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Launch ${PRODUCT_NAME}"
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\RELEASE_NOTES.md"
!define MUI_FINISHPAGE_SHOWREADME_TEXT "View release notes"

!insertmacro MUI_PAGE_LICENSE "${AOA_LICENSE_FILE}"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "${PRODUCT_NAME}" SecGame
  SectionIn RO
  SetOutPath "$INSTDIR"
  File "${AOA_STAGE_DIR}\AgeofAffinities.exe"
  File "${AOA_STAGE_DIR}\*.dll"
  File "${AOA_STAGE_DIR}\assets.dat"
  File "${AOA_STAGE_DIR}\EULA.txt"
  File "${AOA_STAGE_DIR}\RELEASE_NOTES.md"

  SetOutPath "$INSTDIR\scenarios"
  File /r "${AOA_STAGE_DIR}\scenarios\*.*"

  SetOutPath "$INSTDIR\patterns"
  File /r "${AOA_STAGE_DIR}\patterns\*.*"

  SetOutPath "$INSTDIR"
  CreateDirectory "$SMPROGRAMS\${COMPANY_NAME}"
  CreateShortCut "$SMPROGRAMS\${COMPANY_NAME}\${PRODUCT_NAME}.lnk" "$INSTDIR\AgeofAffinities.exe"
  CreateShortCut "$SMPROGRAMS\${COMPANY_NAME}\Release notes.lnk" "$INSTDIR\RELEASE_NOTES.md"
  CreateShortCut "$SMPROGRAMS\${COMPANY_NAME}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  WriteRegStr HKLM "${UNINSTALL_REG}" "DisplayName" "${PRODUCT_NAME}"
  WriteRegStr HKLM "${UNINSTALL_REG}" "DisplayVersion" "${AOA_DISPLAY_VERSION}"
  WriteRegStr HKLM "${UNINSTALL_REG}" "Publisher" "${PRODUCT_PUBLISHER}"
  WriteRegStr HKLM "${UNINSTALL_REG}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_REG}" "DisplayIcon" "$INSTDIR\AgeofAffinities.exe"
  WriteRegStr HKLM "${UNINSTALL_REG}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegStr HKLM "${UNINSTALL_REG}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINSTALL_REG}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_REG}" "NoRepair" 1
SectionEnd

!ifdef AOA_HAS_PATTERN_MAKER
Section /o "Pattern Maker" SecPatternMaker
  SetOutPath "$INSTDIR"
  File "${AOA_STAGE_DIR}\PatternMaker.exe"
  CreateShortCut "$SMPROGRAMS\${COMPANY_NAME}\Pattern Maker.lnk" "$INSTDIR\PatternMaker.exe"
SectionEnd
!endif

Section /o "Desktop shortcut" SecDesktop
  CreateShortCut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\AgeofAffinities.exe"
SectionEnd

!ifdef AOA_VCREDIST_FILE
Section "Visual C++ runtime" SecVcRedist
  SetOutPath "$TEMP"
  File "/oname=aoa-vc-redist.exe" "${AOA_VCREDIST_FILE}"
  ExecWait '"$TEMP\aoa-vc-redist.exe" /install /quiet /norestart'
  Delete "$TEMP\aoa-vc-redist.exe"
SectionEnd
!endif

LangString DESC_SecGame ${LANG_ENGLISH} "Game executable, libraries, packed assets, and release notes."
!ifdef AOA_HAS_PATTERN_MAKER
LangString DESC_SecPatternMaker ${LANG_ENGLISH} "Map pattern editor (optional tool)."
!endif
LangString DESC_SecDesktop ${LANG_ENGLISH} "Shortcut on the desktop."
!ifdef AOA_VCREDIST_FILE
LangString DESC_SecVcRedist ${LANG_ENGLISH} "Microsoft Visual C++ runtime required to run the game."
!endif

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecGame} $(DESC_SecGame)
!ifdef AOA_HAS_PATTERN_MAKER
  !insertmacro MUI_DESCRIPTION_TEXT ${SecPatternMaker} $(DESC_SecPatternMaker)
!endif
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} $(DESC_SecDesktop)
!ifdef AOA_VCREDIST_FILE
  !insertmacro MUI_DESCRIPTION_TEXT ${SecVcRedist} $(DESC_SecVcRedist)
!endif
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP|MB_OK "${PRODUCT_NAME} requires 64-bit Windows."
    Abort
  ${EndIf}
  SetRegView 64

FunctionEnd

Section "Uninstall"
  SetRegView 64
  Delete "$INSTDIR\AgeofAffinities.exe"
  Delete "$INSTDIR\PatternMaker.exe"
  Delete "$INSTDIR\assets.dat"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\EULA.txt"
  Delete "$INSTDIR\RELEASE_NOTES.md"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir /r "$INSTDIR\scenarios"
  RMDir /r "$INSTDIR\patterns"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\${COMPANY_NAME}\${PRODUCT_NAME}.lnk"
  Delete "$SMPROGRAMS\${COMPANY_NAME}\Pattern Maker.lnk"
  Delete "$SMPROGRAMS\${COMPANY_NAME}\Release notes.lnk"
  Delete "$SMPROGRAMS\${COMPANY_NAME}\Uninstall.lnk"
  RMDir "$SMPROGRAMS\${COMPANY_NAME}"
  Delete "$DESKTOP\${PRODUCT_NAME}.lnk"

  DeleteRegKey HKLM "${UNINSTALL_REG}"
SectionEnd
