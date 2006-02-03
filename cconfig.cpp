/* Copyright (C) 2004-2005 Robert Griebl.  All rights reserved.
**
** This file is part of BrickStore.
**
** This file may be distributed and/or modified under the terms of the GNU 
** General Public License version 2 as published by the Free Software Foundation 
** and appearing in the file LICENSE.GPL included in the packaging of this file.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
** See http://fsf.org/licensing/licenses/gpl.html for GPL licensing information.
*/
#include <stdlib.h>

#include <qglobal.h>
#include <qdir.h>

#if defined( Q_WS_WIN )
#include <windows.h>
#include <tchar.h>
#include <shlobj.h>
#endif

#if defined ( Q_WS_MACX )
#include <Carbon/Carbon.h>
#endif

#include "cconfig.h"


namespace {

static inline Q_INT32 mkver ( int a, int b, int c )
{ 
	return ( Q_INT32( a ) << 24 ) | ( Q_INT32( b ) << 16 ) | ( Q_INT32( c ));
}

} // namespace


CConfig::CConfig ( )
	: QObject ( ), QSettings ( )
{
	setPath ( "softforge.de", "BrickStore", QSettings::User );

	m_show_input_errors = readBoolEntry ( "/General/ShowInputErrors", true );
	m_weight_system = ( readEntry ( "/General/WeightSystem", "metric" ) == "metric" ) ? WeightMetric : WeightImperial;
	m_simple_mode = readBoolEntry ( "/General/SimpleMode", false );
	m_window_mode = readBoolEntry ( "/MainWindow/WindowMode", 0 );
}

CConfig::~CConfig ( )
{
	s_inst = 0;
}

CConfig *CConfig::s_inst = 0;

CConfig *CConfig::inst ( )
{
	if ( !s_inst ) {
#if defined( Q_OS_UNIX ) && !defined( Q_OS_MAC )
		// config dirs have to exist, otherwise Qt falls back to the default location (~/.qt/...)
		QDir d = QDir::homeDirPath ( );
		if ( !d. cd ( ".softforge" )) {
			d = QDir::homeDirPath ( );
			if ( !d. mkdir ( ".softforge" ))
				qWarning ( "Could not create config directory: ~/.softforge" );
		}
#endif
		s_inst = new CConfig ( );
	}
	return s_inst;
}


QString CConfig::obscure ( const QString &str )
{
	QString result;
	const QChar *unicode = str. unicode ( );
	for ( uint i = 0; i < str. length ( ); i++ )
		result += ( unicode [i]. unicode ( ) < 0x20 ) ? unicode [i] :
	QChar ( 0x1001F - unicode [i]. unicode ( ));
	return result;
}

QString CConfig::readPasswordEntry ( const QString &key ) const
{
	return obscure ( readEntry ( key, QString ( "" )));
}

bool CConfig::writePasswordEntry ( const QString &key, const QString &password )
{
	return writeEntry ( key, obscure ( password ));
}


void CConfig::upgrade ( int vmajor, int vminor, int vrev )
{
	QStringList sl;
	QString s;
	int i;
	bool b;
	bool ok;

	int cfgver = readNumEntry ( "/ConfigVersion", 0 );
	writeEntry ( "/ConfigVersion", mkver ( vmajor, vminor, vrev ));

	if ( cfgver < mkver ( 1, 0, 125 )) {
		// convert itemview column info (>= 1.0.25)
		beginGroup ( "/ItemView/List" );

		sl = readListEntry ( "/ColumnWidths", &ok );
		if ( ok )  writeEntry ( "/ColumnWidths", sl. join ( "," ));

		sl = readListEntry ( "/ColumnWidthsHidden", &ok );
		if ( ok )  writeEntry ( "/ColumnWidthsHidden", sl. join ( "," ));

		sl = readListEntry ( "/ColumnOrder", &ok );
		if ( ok )  writeEntry ( "/ColumnOrder", sl. join ( "," ));

		i = readNumEntry ( "/SortColumn", -1, &ok );
		if ( ok )  writeEntry ( "/SortColumn", QString::number ( i ));

		b = readBoolEntry ( "/SortAscending", true, &ok );
		if ( ok )  writeEntry ( "/SortDirection", b ? "A" : "D" );
		removeEntry ( "/SortAscending" );

		endGroup ( );
	
		// fix a typo (>= 1.0.125)
		s = readEntry ( "/Default/AddItems/Condition", QString::null, &ok );
		if ( ok )  writeEntry ( "/Defaults/AddItems/Condition", s );
		removeEntry ( "/Default/AddItems/Condition" );
	}
}


bool CConfig::showInputErrors ( ) const
{
	return m_show_input_errors;
}

void CConfig::setShowInputErrors ( bool b )
{
	if ( b != m_show_input_errors ) {
		m_show_input_errors = b;
		writeEntry ( "/General/ShowInputErrors", b );

		emit showInputErrorsChanged ( b );
	}
}

void CConfig::setLDrawDir ( const QString &dir )
{
	writeEntry ( "/General/LDrawDir", dir );
}

QString CConfig::lDrawDir ( ) const
{
	QString dir = readEntry ( "/General/LDrawDir" );

	if ( dir. isEmpty ( ))
		dir = ::getenv ( "LDRAWDIR" );

#if defined( Q_WS_WIN )
	if ( dir. isEmpty ( )) {
		QT_WA( {
			WCHAR inidir [MAX_PATH];

			DWORD l = GetPrivateProfileStringW ( L"LDraw", L"BaseDirectory", L"", inidir, MAX_PATH, L"ldraw.ini" );
			if ( l >= 0 ) {
				inidir [l] = 0;
				dir = QString::fromUcs2 ( inidir );
			}
		}, {
			char inidir [MAX_PATH];

			DWORD l = GetPrivateProfileStringA ( "LDraw", "BaseDirectory", "", inidir, MAX_PATH, "ldraw.ini" );
			if ( l >= 0 ) {
				inidir [l] = 0;
				dir = QString::fromLocal8Bit ( inidir );
			}
		})
	}

	if ( dir. isEmpty ( )) {
		HKEY skey, lkey;

		QT_WA( {
			if ( RegOpenKeyExW ( HKEY_LOCAL_MACHINE, L"Software", 0, KEY_READ, &skey ) == ERROR_SUCCESS ) {
				if ( RegOpenKeyExW ( skey, L"LDraw", 0, KEY_READ, &lkey ) == ERROR_SUCCESS ) {
					WCHAR regdir [MAX_PATH + 1];
					DWORD regdirsize = MAX_PATH * sizeof( WCHAR );

					if ( RegQueryValueExW ( lkey, L"InstallDir", 0, 0, (LPBYTE) &regdir, &regdirsize ) == ERROR_SUCCESS ) {
						regdir [regdirsize / sizeof( WCHAR )] = 0;
						dir = QString::fromUcs2 ( regdir );
					}
					RegCloseKey ( lkey );
				}
				RegCloseKey ( skey );
			}
		}, {
			if ( RegOpenKeyExA ( HKEY_LOCAL_MACHINE, "Software", 0, KEY_READ, &skey ) == ERROR_SUCCESS ) {
				if ( RegOpenKeyExA ( skey, "LDraw", 0, KEY_READ, &lkey ) == ERROR_SUCCESS ) {
					char regdir [MAX_PATH + 1];
					DWORD regdirsize = MAX_PATH;

					if ( RegQueryValueExA ( lkey, "InstallDir", 0, 0, (LPBYTE) &regdir, &regdirsize ) == ERROR_SUCCESS ) {
						regdir [regdirsize] = 0;
						dir = QString::fromLocal8Bit ( regdir );
					}
					RegCloseKey ( lkey );
				}
				RegCloseKey ( skey );
			}
		})
	}
#endif
	return dir;
}

QString CConfig::documentDir ( ) const
{
	QString dir = readEntry ( "/General/DocDir" );

	if ( dir. isEmpty ( )) {
		dir = QDir::homeDirPath ( );

#if defined( Q_WS_WIN )	
		QT_WA( {
			WCHAR wpath [MAX_PATH];

			if ( SHGetSpecialFolderPathW ( NULL, wpath, CSIDL_PERSONAL, TRUE )) 
				dir = QString::fromUcs2 ( wpath );
		}, {
			char apath [MAX_PATH];
			
			if ( SHGetSpecialFolderPathA ( NULL, apath, CSIDL_PERSONAL, TRUE ))
				dir = QString::fromLocal8Bit ( apath );
		} )
		
#elif defined( Q_WS_MACX )
		FSRef dref;
		
		if ( FSFindFolder( kUserDomain, kDocumentsFolderType, kDontCreateFolder, &dref ) == noErr ) {
			UInt8 strbuffer [PATH_MAX];
		
			if ( FSRefMakePath ( &dref, strbuffer, sizeof( strbuffer )) == noErr )
				dir = QString::fromUtf8 ( reinterpret_cast <char *> ( strbuffer ));
 		}
		
#endif
	}
	return dir;
}

void CConfig::setDocumentDir ( const QString &dir )
{
	writeEntry ( "/General/DocDir", dir );
}


bool CConfig::useProxy ( ) const
{
	return readBoolEntry ( "/Internet/UseProxy", false );
}

QString CConfig::proxyName ( ) const
{
	return readEntry ( "/Internet/ProxyName" );
}

int CConfig::proxyPort ( ) const
{
	return readNumEntry ( "/Internet/ProxyPort", 8080 );
}

void CConfig::setProxy ( bool b, const QString &name, int port )
{
	bool ob = useProxy ( );
	QString oname = proxyName ( );
	int oport = proxyPort ( );
	
	if (( b != ob ) || ( oname != name ) || ( oport != port )) {
		writeEntry ( "/Internet/UseProxy", b );
		writeEntry ( "/Internet/ProxyName", name );
		writeEntry ( "/Internet/ProxyPort", port );
		
		emit proxyChanged ( b, name, port );
	}
}


QString CConfig::dataDir ( ) const
{
	return readEntry ( "/BrickLink/DataDir" );
}

void CConfig::setDataDir ( const QString &dir )
{
	writeEntry ( "/BrickLink/DataDir", dir );
}

QString CConfig::language ( ) const
{
	return readEntry ( "/General/Locale" );
}

void CConfig::setLanguage ( const QString &lang )
{
	QString old = language ( );

	if ( old != lang ) {
		writeEntry ( "/General/Locale", lang );

		emit languageChanged ( );
	}
}

CConfig::WeightSystem CConfig::weightSystem ( ) const
{
	return m_weight_system;
}

void CConfig::setWeightSystem ( WeightSystem ws )
{
	if ( ws != m_weight_system ) {
		m_weight_system = ws;
		writeEntry ( "/General/WeightSystem", ws == WeightMetric ? "metric" : "imperial" );

		emit weightSystemChanged ( ws );
	}
}


bool CConfig::simpleMode ( ) const
{
	return m_simple_mode;
}

void CConfig::setSimpleMode ( bool sm  )
{
	if ( sm != m_simple_mode ) {
		m_simple_mode = sm;
		writeEntry ( "/General/SimpleMode", sm );

		emit simpleModeChanged ( sm );
	}
}


int CConfig::windowMode ( ) const
{
	return m_window_mode;
}

void CConfig::setWindowMode ( int wm )
{
	if ( wm != m_window_mode ) {
		m_window_mode = wm;
		writeEntry ( "/MainWindow/WindowMode", wm );

		emit windowModeChanged ( wm );
	}
}


void CConfig::blUpdateIntervals ( int &pic, int &pg ) const
{
	int picd, pgd;
	
	blUpdateIntervalsDefaults ( picd, pgd );

	pic = CConfig::inst ( )-> readNumEntry ( "/BrickLink/UpdateInterval/Pictures",    picd );
	pg  = CConfig::inst ( )-> readNumEntry ( "/BrickLink/UpdateInterval/PriceGuides", pgd  );
}

void CConfig::blUpdateIntervalsDefaults ( int &picd, int &pgd ) const
{
	int day2sec = 60*60*24;

	picd = 180 * day2sec;
	pgd  =  14 * day2sec;
}

void CConfig::setBlUpdateIntervals ( int pic, int pg )
{
	int opic, opg;
	
	blUpdateIntervals ( opic, opg );
	
	if (( opic != pic ) || ( opg != pg )) {
		writeEntry ( "/BrickLink/UpdateInterval/Pictures",    pic );
		writeEntry ( "/BrickLink/UpdateInterval/PriceGuides", pg  );		

		emit blUpdateIntervalsChanged ( pic, pg );
	}
}


QString CConfig::blLoginUsername ( ) const
{
	return readEntry ( "/BrickLink/Login/Username" );
}

void CConfig::setBlLoginUsername ( const QString &name )
{
	writeEntry ( "/BrickLink/Login/Username", name );
}


QString CConfig::blLoginPassword ( ) const
{
	return readPasswordEntry ( "/BrickLink/Login/Password" );
}

void CConfig::setBlLoginPassword ( const QString &pass )
{
	writePasswordEntry ( "/BrickLink/Login/Password", pass );
}


bool CConfig::onlineStatus ( ) const
{
	return readBoolEntry ( "/Internet/Online", true );
}

void CConfig::setOnlineStatus ( bool b )
{
	bool ob = onlineStatus ( );
	
	if ( b != ob ) {
		writeEntry ( "/Internet/Online", b );
	
		emit onlineStatusChanged ( b );
	}
}


int CConfig::infoBarLook ( ) const
{
	return readNumEntry ( "/MainWindow/Infobar/Look", 1 ); // HACK: CInfoBar::ModernLeft
}

void CConfig::setInfoBarLook ( int look )
{
	int olook = infoBarLook ( );

	if ( look != olook ) {
		writeEntry ( "/MainWindow/Infobar/Look", look );
		
		emit infoBarLookChanged ( look );
	}
}

