## Copyright (C) 2004-2008 Robert Griebl.  All rights reserved.
##
## This file is part of BrickStore.
##
## This file may be distributed and/or modified under the terms of the GNU
## General Public License version 2 as published by the Free Software Foundation
## and appearing in the file LICENSE.GPL included in the packaging of this file.
##
## This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
## WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
##
## See http://fsf.org/licensing/licenses/gpl.html for GPL licensing information.

isEmpty( RELEASE ) {
  RELEASE    = 2.0.0
}

TEMPLATE     = app
CONFIG      *= warn_on thread qt
# CONFIG      *= modeltest
QT          *= core gui xml network script

static:QTPLUGIN  *= qjpeg qgif
static:DEFINES += STATIC_QT_BUILD

TARGET            = BrickStore
unix:!macx:TARGET = brickstore

LANGUAGES    = de fr nl sl
RESOURCES   += brickstore.qrc
SUBPROJECTS  = utility bricklink ldraw lzma

modeltest:debug:SUBPROJECTS += modeltest


SOURCES += \
  appearsinwidget.cpp \
  application.cpp \
  config.cpp \
  document.cpp \
  documentdelegate.cpp \
  framework.cpp \
  itemdetailpopup.cpp \
  main.cpp \
  picturewidget.cpp \
  priceguidewidget.cpp \
  rebuilddatabase.cpp \
  report.cpp \
  reportobjects.cpp \
  selectcolor.cpp \
  selectitem.cpp \
  splash.cpp \
  taskwidgets.cpp \
  window.cpp \

HEADERS += \
  appearsinwidget.h \
  application.h \
  checkforupdates.h \
  config.h \
  document.h \
  document_p.h \
  documentdelegate.h \
  framework.h \
  import.h \
  itemdetailpopup.h \
  picturewidget.h \
  priceguidewidget.h \
  rebuilddatabase.h \
  ref.h \
  report.h \
  report_p.h \
  reportobjects.h \
  selectcolor.h \
  selectitem.h \
  splash.h \
  taskwidgets.h \
  updatedatabase.h \
  window.h \


FORMS = \
  additemdialog.ui \
  consolidateitemsdialog.ui \
  importinventorydialog.ui \
  importorderdialog.ui \
  incdecpricesdialog.ui \
  incompleteitemdialog.ui \
  informationdialog.ui \
  selectcolordialog.ui \
  selectdocumentdialog.ui \
  selectitemdialog.ui \
  selectreportdialog.ui \
  settingsdialog.ui \
  settopriceguidedialog.ui \

HEADERS += $$replace(FORMS, '.ui$', '.h')
SOURCES += $$replace(FORMS, '.ui$', '.cpp')

# for(subp, SUBPROJECTS) : include($${subp}/$${subp}.pri)
# workaround until creator is able to cope with for()
include('utility/utility.pri')
include('bricklink/bricklink.pri')
include('ldraw/ldraw.pri')
include('lzma/lzma.pri')
modeltest:debug:include('modeltest/modeltest.pri')

TRANSLATIONS = $$replace(LANGUAGES, '$', '.ts')
TRANSLATIONS = $$replace(TRANSLATIONS, '^', 'translations/brickstore_')


#
# (n)make tarball
#

DISTFILES += $$SOURCES $$HEADERS $$FORMS $$RESOURCES

DISTFILES += brickstore.rc brickstore.ico brickstore_doc.ico
DISTFILES += version.h.in icon.png LICENSE.GPL brickstore.pro
for(subp, SUBPROJECTS) : DISTFILES += $${subp}/$${subp}.pri

DISTFILES += images/*.png images/*.jpg images/*.pic images/16x16/*.png images/22x22/*.png images/status/*.png images/sidebar/*.png
DISTFILES += utility/images/*.png
DISTFILES += translations/translations.xml $$TRANSLATIONS $$replace(TRANSLATIONS, .ts, .qm)
DISTFILES += print-templates/*.qs

DISTFILES += scripts/*.sh scripts/*.pl scripts/*.js
DISTFILES += unix/build-package.sh unix/deb/build-package.sh unix/rpm/build-package.sh \
             unix/rpm/brickstore.spec unix/deb/rules \
             unix/share/mimelnk/application/x-brickstore-xml.desktop \
             unix/share/icons/hicolor/48x48/mimetypes/application-x-brickstore-xml.png \
             unix/share/icons/hicolor/64x64/apps/brickstore.png \
             unix/share/mime/packages/brickstore-mime.xml \
             unix/share/applications/brickstore.desktop
DISTFILES += win32/installer/*.wx? win32/build-package.bat win32/installer/7zS.ini win32/installer/VsSetup.ini win32/tools/* win32/installer/Binary/*
DISTFILES += macx/build-package.sh macx/install-table.txt macx/*.plist macx/Resources/*.icns
for(lang, LANGUAGES) : DISTFILES += macx/Resources/$${lang}.lproj/*.plist

unix {
  #tarball.target = $$lower($$TARGET)-$$RELEASE.tar.bz2
  tarball.commands = ( rel=$(RELEASE) ; dst=$$lower($$TARGET)-\$${rel:-$$RELEASE}; \
                       rm -rf \$$dst ; \
                       mkdir \$$dst ; \
                       for i in $$DISTFILES; do \
                           j=\$$dst/`dirname \$$i`; \
                           [ -d \$$j ] || mkdir -p \$$j; \
                           cp \$$i \$$j; \
                       done ; \
                       tar -cjf \$$dst.tar.bz2 \$$dst ; \
                       rm -rf \$$dst )

  macx {
    package.commands = macx/build-package.sh
  } else {
    package.commands = unix/build-package.sh
  }
}

win32 {
  DISTFILES=$$replace(DISTFILES, /, \\)
  DISTFILES=$$replace(DISTFILES, ^\.\\\, ) # 7-zip doesn't like file names starting with dot-backslash

  tarball.commands = ( DEL $$lower($$TARGET)-$${RELEASE}.zip 2>NUL & win32\tools\7za a -tzip $$lower($$TARGET)-$${RELEASE}.zip $$DISTFILES )

  package.commands = win32\build-package.bat
}

QMAKE_EXTRA_TARGETS += tarball package


#
# create version.h
#

win32 {
  system( cscript.exe //B scripts\update_version.js $$RELEASE)
}

unix {
  system( scripts/update_version.sh $$RELEASE)
}

#
# Windows specific
#

win32 {
  CONFIG  += windows
  #CONFIG -= shared

  RC_FILE  = brickstore.rc

  DEFINES += __USER__=\"$$(USERNAME)\" __HOST__=\"$$(COMPUTERNAME)\"

  win32-msvc* {
    QMAKE_CXXFLAGS_DEBUG   += /Od /GL-
    QMAKE_CXXFLAGS_RELEASE += /O2 /GL
  }

  win32-msvc2005 {
     DEFINES += _CRT_SECURE_NO_DEPRECATE

#    QMAKE_LFLAGS_WINDOWS += "/MANIFEST:NO"
#    QMAKE_LFLAGS_WINDOWS += "/LTCG"

     QMAKE_CXXFLAGS_DEBUG   += /EHc- /EHs- /GR-
     QMAKE_CXXFLAGS_RELEASE += /EHc- /EHs- /GR-
  }
  LIBS += user32.lib advapi32.lib
}


#
# Unix specific
#

unix {
  MOC_DIR     = .moc
  UI_DIR      = .uic
  RCC_DIR     = .rcc
  OBJECTS_DIR = .obj

  DEFINES += __USER__=\"$$(USER)\" __HOST__=\"$$system( hostname )\"
}


#
# Unix/X11 specific
#

unix:!macx {
  CONFIG += x11

  isEmpty( PREFIX ):PREFIX = /usr/local
  target.path = $$PREFIX/bin
  INSTALLS += target
}


#
# Mac OS X specific
#

macx {
  CONFIG += x86

  QMAKE_INFO_PLIST = macx/Info.plist
  bundle_icons.files = $$system(find macx/Resources/ -name '*.icns')
  bundle_icons.path = Contents/Resources
  bundle_locversions.files = $$system(find macx/Resources/ -name '*.lproj')
  bundle_locversions.path = Contents/Resources
  QMAKE_BUNDLE_DATA += bundle_icons bundle_locversions
}

