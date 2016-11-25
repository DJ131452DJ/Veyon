/*
 * MainWindow.cpp - implementation of MainWindow class
 *
 * Copyright (c) 2010-2016 Tobias Doerffel <tobydox/at/users/dot/sf/dot/net>
 *
 * This file is part of iTALC - http://italc.sourceforge.net
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include <italcconfig.h>

#ifdef ITALC_BUILD_WIN32
#include <windows.h>
void Win32AclEditor( HWND hwnd );
#endif

#include <QtCore/QDir>
#include <QtCore/QProcess>
#include <QtCore/QTimer>
#include <QtGui/QCloseEvent>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QProgressBar>
#include <QProgressDialog>

#include "Configuration/XmlStore.h"
#include "Configuration/UiMapping.h"

#include "Ldap/LdapDirectory.h"

#include "AboutDialog.h"
#include "KeyFileAssistant.h"
#include "FileSystemBrowser.h"
#include "ImcCore.h"
#include "ItalcConfiguration.h"
#include "LocalSystem.h"
#include "Logger.h"
#include "LogonAclSettings.h"
#include "LogonAuthentication.h"
#include "LogonGroupEditor.h"
#include "MainWindow.h"
#include "PasswordDialog.h"

#include "ui_MainWindow.h"



MainWindow::MainWindow() :
	QMainWindow(),
	ui( new Ui::MainWindow ),
	m_configChanged( false )
{
	ui->setupUi( this );

	setWindowTitle( tr( "iTALC Management Console %1" ).arg( ITALC_VERSION ) );

	// retrieve list of builtin translations and populate language combobox
	QStringList languages;
	for( auto language : QDir(":/resources/").entryList( QStringList("*.qm") ) )
	{
		QLocale loc(language);
		if( loc.language() == QLocale::C )
		{
			loc = QLocale( QLocale::English );
		}
		languages += QString( "%1 - %2 (%3)" ).arg( QLocale::languageToString(loc.language() ),
													loc.nativeLanguageName(),
													loc.name() );
	}

	qSort( languages );

	ui->uiLanguage->addItems( languages );

	// reset all widget's values to current configuration
	reset();

	// if local configuration is incomplete, re-enable the apply button
	if( ItalcConfiguration(
			Configuration::Store::LocalBackend ).data().size() <
										ItalcCore::config->data().size() )
	{
		configurationChanged();
	}

	// connect widget signals to configuration property write methods
	FOREACH_ITALC_UI_CONFIG_PROPERTY(CONNECT_WIDGET_TO_PROPERTY);
	FOREACH_ITALC_SERVICE_CONFIG_PROPERTY(CONNECT_WIDGET_TO_PROPERTY);
	FOREACH_ITALC_LOGGING_CONFIG_PROPERTY(CONNECT_WIDGET_TO_PROPERTY);
	FOREACH_ITALC_VNC_SERVER_CONFIG_PROPERTY(CONNECT_WIDGET_TO_PROPERTY);
	FOREACH_ITALC_DEMO_SERVER_CONFIG_PROPERTY(CONNECT_WIDGET_TO_PROPERTY);
	FOREACH_ITALC_NETWORK_CONFIG_PROPERTY(CONNECT_WIDGET_TO_PROPERTY);
	FOREACH_ITALC_CONFIG_FILE_PATHS_CONFIG_PROPERTY(CONNECT_WIDGET_TO_PROPERTY);
	FOREACH_ITALC_DATA_DIRECTORIES_CONFIG_PROPERTY(CONNECT_WIDGET_TO_PROPERTY);
	FOREACH_ITALC_AUTHENTICATION_CONFIG_PROPERTY(CONNECT_WIDGET_TO_PROPERTY);
	FOREACH_ITALC_LDAP_CONFIG_PROPERTY(CONNECT_WIDGET_TO_PROPERTY);

	for( auto page : findChildren<ConfigurationPage *>() )
	{
		page->connectWidgetsToProperties();
	}

#define CONNECT_BUTTON_SLOT(name) \
			connect( ui->name, SIGNAL( clicked() ), this, SLOT( name() ) );

	CONNECT_BUTTON_SLOT( startService );
	CONNECT_BUTTON_SLOT( stopService );

	CONNECT_BUTTON_SLOT( openLogFileDirectory );
	CONNECT_BUTTON_SLOT( clearLogFiles );

	CONNECT_BUTTON_SLOT( openGlobalConfig );
	CONNECT_BUTTON_SLOT( openPersonalConfig );
	CONNECT_BUTTON_SLOT( openSnapshotDirectory );

	CONNECT_BUTTON_SLOT( openPublicKeyBaseDir );
	CONNECT_BUTTON_SLOT( openPrivateKeyBaseDir );

	CONNECT_BUTTON_SLOT( launchKeyFileAssistant );
	CONNECT_BUTTON_SLOT( manageACLs );
	CONNECT_BUTTON_SLOT( testLogonAuthentication );

	CONNECT_BUTTON_SLOT( testLdapBind );
	CONNECT_BUTTON_SLOT( testLdapBaseDn );
	CONNECT_BUTTON_SLOT( testLdapNamingContext );
	CONNECT_BUTTON_SLOT( testLdapUserTree );
	CONNECT_BUTTON_SLOT( testLdapGroupTree );
	CONNECT_BUTTON_SLOT( testLdapComputerTree );

	CONNECT_BUTTON_SLOT( testLdapUserLoginAttribute );
	CONNECT_BUTTON_SLOT( testLdapGroupMemberAttribute );
	CONNECT_BUTTON_SLOT( testLdapComputerHostNameAttribute );

	CONNECT_BUTTON_SLOT( testLdapUsersFilter );
	CONNECT_BUTTON_SLOT( testLdapUserGroupsFilter );
	CONNECT_BUTTON_SLOT( testLdapComputerGroupsFilter );

	CONNECT_BUTTON_SLOT( testLdapComputerPoolAttribute );

	CONNECT_BUTTON_SLOT( testLdapGroupsOfUser );
	CONNECT_BUTTON_SLOT( testLdapGroupsOfComputer );
	CONNECT_BUTTON_SLOT( testLdapComputerPoolMembers );
	CONNECT_BUTTON_SLOT( testLdapCommonAggregations );

	CONNECT_BUTTON_SLOT( generateBugReportArchive );

	connect( ui->buttonBox, SIGNAL( clicked( QAbstractButton * ) ),
				this, SLOT( resetOrApply( QAbstractButton * ) ) );

	connect( ui->actionLoadSettings, SIGNAL( triggered() ),
				this, SLOT( loadSettingsFromFile() ) );
	connect( ui->actionSaveSettings, SIGNAL( triggered() ),
				this, SLOT( saveSettingsToFile() ) );

	connect( ui->actionAboutQt, SIGNAL( triggered() ),
				QApplication::instance(), SLOT( aboutQt() ) );

	updateServiceControl();

	QTimer *serviceUpdateTimer = new QTimer( this );
	serviceUpdateTimer->start( 2000 );

	connect( serviceUpdateTimer, SIGNAL( timeout() ),
				this, SLOT( updateServiceControl() ) );

	connect( ItalcCore::config, SIGNAL( configurationChanged() ),
				this, SLOT( configurationChanged() ) );

#ifndef ITALC_BUILD_WIN32
	ui->logToWindowsEventLog->hide();
#endif
}




MainWindow::~MainWindow()
{
}



void MainWindow::reset( bool onlyUI )
{
	if( onlyUI == false )
	{
		ItalcCore::config->clear();
		*ItalcCore::config += ItalcConfiguration::defaultConfiguration();
		*ItalcCore::config += ItalcConfiguration( Configuration::Store::LocalBackend );
	}

#ifdef ITALC_BUILD_WIN32
	// always make sure we do not have a LogonACL string in our config
	ItalcCore::config->removeValue( "LogonACL", "Authentication" );

	// revert LogonACL to what has been saved in the encoded logon ACL
	LogonAclSettings().setACL(
		ItalcCore::config->value( "EncodedLogonACL", "Authentication" ) );
#endif

	FOREACH_ITALC_UI_CONFIG_PROPERTY(INIT_WIDGET_FROM_PROPERTY);
	FOREACH_ITALC_SERVICE_CONFIG_PROPERTY(INIT_WIDGET_FROM_PROPERTY);
	FOREACH_ITALC_LOGGING_CONFIG_PROPERTY(INIT_WIDGET_FROM_PROPERTY);
	FOREACH_ITALC_VNC_SERVER_CONFIG_PROPERTY(INIT_WIDGET_FROM_PROPERTY);
	FOREACH_ITALC_DEMO_SERVER_CONFIG_PROPERTY(INIT_WIDGET_FROM_PROPERTY);
	FOREACH_ITALC_NETWORK_CONFIG_PROPERTY(INIT_WIDGET_FROM_PROPERTY);
	FOREACH_ITALC_CONFIG_FILE_PATHS_CONFIG_PROPERTY(INIT_WIDGET_FROM_PROPERTY);
	FOREACH_ITALC_DATA_DIRECTORIES_CONFIG_PROPERTY(INIT_WIDGET_FROM_PROPERTY);
	FOREACH_ITALC_AUTHENTICATION_CONFIG_PROPERTY(INIT_WIDGET_FROM_PROPERTY);
	FOREACH_ITALC_LDAP_CONFIG_PROPERTY(INIT_WIDGET_FROM_PROPERTY);

	for( auto page : findChildren<ConfigurationPage *>() )
	{
		page->resetWidgets();
	}

	ui->buttonBox->setEnabled( false );
	m_configChanged = false;
}




void MainWindow::apply()
{
#ifdef ITALC_BUILD_WIN32
	ItalcCore::config->setValue( "EncodedLogonACL", LogonAclSettings().acl(),
															"Authentication" );
#endif
	if( ImcCore::applyConfiguration( *ItalcCore::config ) )
	{
#ifdef ITALC_BUILD_WIN32
		if( isServiceRunning() &&
			QMessageBox::question( this, tr( "Restart iTALC Service" ),
				tr( "All settings were saved successfully. In order to take "
					"effect the iTALC service needs to be restarted. "
					"Restart it now?" ), QMessageBox::Yes | QMessageBox::No,
				QMessageBox::Yes ) == QMessageBox::Yes )
		{
			stopService();
			startService();
		}
#endif
		ui->buttonBox->setEnabled( false );
		m_configChanged = false;
	}
}




void MainWindow::configurationChanged()
{
	ui->buttonBox->setEnabled( true );
	m_configChanged = true;
}




void MainWindow::resetOrApply( QAbstractButton *btn )
{
	if( ui->buttonBox->standardButton( btn ) & QDialogButtonBox::Apply )
	{
		apply();
	}
	else if( ui->buttonBox->standardButton( btn ) & QDialogButtonBox::Reset )
	{
		reset();
	}
}




void MainWindow::startService()
{
	serviceControlWithProgressBar( tr( "Starting iTALC service" ), "-startservice" );
}




void MainWindow::stopService()
{
	serviceControlWithProgressBar( tr( "Stopping iTALC service" ), "-stopservice" );
}




void MainWindow::updateServiceControl()
{
	bool running = isServiceRunning();
#ifdef ITALC_BUILD_WIN32
	ui->startService->setEnabled( !running );
	ui->stopService->setEnabled( running );
#else
	ui->startService->setEnabled( false );
	ui->stopService->setEnabled( false );
#endif
	ui->serviceState->setText( running ? tr( "Running" ) : tr( "Stopped" ) );
}




void MainWindow::openLogFileDirectory()
{
	FileSystemBrowser( FileSystemBrowser::ExistingDirectory ).
												exec( ui->logFileDirectory );
}




void MainWindow::clearLogFiles()
{
#ifdef ITALC_BUILD_WIN32
	bool stopped = false;
	if( isServiceRunning() )
	{
		if( QMessageBox::question( this, tr( "iTALC Service" ),
				tr( "The iTALC service needs to be stopped temporarily "
					"in order to remove the log files. Continue?"
					), QMessageBox::Yes | QMessageBox::No,
				QMessageBox::Yes ) == QMessageBox::Yes )
		{
			stopService();
			stopped = true;
		}
		else
		{
			return;
		}
	}
#endif

	bool success = true;
	QDir d( LocalSystem::Path::expand( ItalcCore::config->logFileDirectory() ) );
	foreach( const QString &f, d.entryList( QStringList() << "Italc*.log" ) )
	{
		if( f != "ItalcManagementConsole.log" )
		{
			success &= d.remove( f );
		}
	}

#ifdef ITALC_BUILD_WIN32
	d = QDir( "C:\\Windows\\Temp" );
#else
	d = QDir( "/tmp" );
#endif

	foreach( const QString &f, d.entryList( QStringList() << "Italc*.log" ) )
	{
		if( f != "ItalcManagementConsole.log" )
		{
			success &= d.remove( f );
		}
	}

#ifdef ITALC_BUILD_WIN32
	if( stopped )
	{
		startService();
	}
#endif

	if( success )
	{
		QMessageBox::information( this, tr( "Log files cleared" ),
			tr( "All log files were cleared successfully." ) );
	}
	else
	{
		QMessageBox::critical( this, tr( "Error" ),
			tr( "Could not remove all log files." ) );
	}
}




void MainWindow::openGlobalConfig()
{
	FileSystemBrowser( FileSystemBrowser::ExistingFile ).
										exec( ui->globalConfigurationPath );
}




void MainWindow::openPersonalConfig()
{
	FileSystemBrowser( FileSystemBrowser::ExistingFile ).
										exec( ui->personalConfigurationPath );
}




void MainWindow::openSnapshotDirectory()
{
	FileSystemBrowser( FileSystemBrowser::ExistingDirectory ).
												exec( ui->snapshotDirectory );
}




void MainWindow::openPublicKeyBaseDir()
{
	FileSystemBrowser( FileSystemBrowser::ExistingDirectory ).
												exec( ui->publicKeyBaseDir );
}




void MainWindow::openPrivateKeyBaseDir()
{
	FileSystemBrowser( FileSystemBrowser::ExistingDirectory ).
												exec( ui->privateKeyBaseDir );
}




void MainWindow::loadSettingsFromFile()
{
	QString fileName = QFileDialog::getOpenFileName( this, tr( "Load settings from file" ),
											QDir::homePath(), tr( "XML files (*.xml)" ) );
	if( !fileName.isEmpty() )
	{
		// write current configuration to output file
		Configuration::XmlStore( Configuration::XmlStore::System,
										fileName ).load( ItalcCore::config );
		reset( true );
		configurationChanged();	// give user a chance to apply possible changes
	}
}




void MainWindow::saveSettingsToFile()
{
	QString fileName = QFileDialog::getSaveFileName( this, tr( "Save settings to file" ),
											QDir::homePath(), tr( "XML files (*.xml)" ) );
	if( !fileName.isEmpty() )
	{
		if( !fileName.endsWith( ".xml", Qt::CaseInsensitive ) )
		{
			fileName += ".xml";
		}

		bool configChangedPrevious = m_configChanged;

#ifdef ITALC_BUILD_WIN32
		ItalcCore::config->removeValue( "LogonACL", "Authentication" );
		ItalcCore::config->setValue( "EncodedLogonACL",
								LogonAclSettings().acl(), "Authentication" );
#endif

		// write current configuration to output file
		Configuration::XmlStore( Configuration::XmlStore::System,
										fileName ).flush( ItalcCore::config );

		m_configChanged = configChangedPrevious;
		ui->buttonBox->setEnabled( m_configChanged );
	}
}




void MainWindow::launchKeyFileAssistant()
{
	KeyFileAssistant().exec();
}




void MainWindow::manageACLs()
{
#ifdef ITALC_BUILD_WIN32
	Win32AclEditor( LocalSystem::getHWNDForWidget( this ) );

	if( LogonAclSettings().acl() !=
				ItalcCore::config->value( "EncodedLogonACL", "Authentication" ) )
	{
		configurationChanged();
	}
#else
	LogonGroupEditor( this ).exec();
#endif
}




void MainWindow::testLogonAuthentication()
{
	PasswordDialog dlg( this );
	if( dlg.exec() )
	{
		bool result = LogonAuthentication::authenticateUser( dlg.credentials() );
		if( result )
		{
			QMessageBox::information( this, tr( "Logon authentication test" ),
							tr( "Authentication with provided credentials "
								"was successful." ) );
		}
		else
		{
			QMessageBox::critical( this, tr( "Logon authentication test" ),
							tr( "Authentication with provided credentials "
								"failed!" ) );
		}
	}
}



bool MainWindow::testLdapBind(bool reportSuccess )
{
	qDebug() << "[TEST][LDAP] Testing bind";

	LdapDirectory ldapDirectory;

	if( ldapDirectory.isConnected() == false )
	{
		QMessageBox::critical( this, tr( "LDAP connection failed"),
							   tr( "Could not connect to the LDAP server. "
								   "Please check the server parameters. "
								   "%1" ).arg( ldapDirectory.ldapErrorDescription() ) );
	}
	else if( ldapDirectory.isBound() == false )
	{
		QMessageBox::critical( this, tr( "LDAP bind failed"),
							   tr( "Could not bind to the LDAP server. "
								   "Please check the server parameters "
								   "and bind credentials. "
								   "%1" ).arg( ldapDirectory.ldapErrorDescription() ) );
	}
	else if( reportSuccess )
	{
		QMessageBox::information( this, tr( "LDAP bind successful"),
								  tr( "Successfully connected to the LDAP "
									  "server and performed an LDAP bind. "
									  "The basic LDAP settings are "
									  "configured correctly." ) );
	}

	return ldapDirectory.isConnected() && ldapDirectory.isBound();
}



void MainWindow::testLdapBaseDn()
{
	if( testLdapBind( false ) )
	{
		qDebug() << "[TEST][LDAP] Testing base DN";

		LdapDirectory ldapDirectory;
		QStringList entries = ldapDirectory.queryBaseDn();

		if( entries.isEmpty() )
		{
			QMessageBox::critical( this, tr( "LDAP base DN test failed"),
								   tr( "Could not query the configured base DN. "
									   "Please check the base DN parameter.\n"
									   "%1" ).arg( ldapDirectory.ldapErrorDescription() ) );
		}
		else
		{
			QMessageBox::information( this, tr( "LDAP base DN test successful" ),
							tr( "The LDAP base DN has been queried successfully. "
								"The following entries were found:\n%1" ).
									  arg( entries.join("\n") ) );
		}
	}
}



void MainWindow::testLdapNamingContext()
{
	if( testLdapBind( false ) )
	{
		qDebug() << "[TEST][LDAP] Testing naming context";

		LdapDirectory ldapDirectory;
		QString baseDn = ldapDirectory.queryNamingContext();

		if( baseDn.isEmpty() )
		{
			QMessageBox::critical( this, tr( "LDAP naming context test failed"),
								   tr( "Could not query the base DN via naming contexts. "
									   "Please check the naming context attribute parameter.\n"
									   "%1" ).arg( ldapDirectory.ldapErrorDescription() ) );
		}
		else
		{
			QMessageBox::information( this, tr( "LDAP naming context test successful" ),
							tr( "The LDAP naming context has been queried successfully. "
								"The following base DN was found:\n%1" ).
									  arg( baseDn ) );
		}
	}
}



void MainWindow::testLdapUserTree()
{
	if( testLdapBind( false ) )
	{
		qDebug() << "[TEST][LDAP] Testing user tree";

		LdapDirectory ldapDirectory;
		ldapDirectory.disableFilters();
		int count = ldapDirectory.users().count();

		reportLdapTreeQueryResult( tr( "user tree" ), count, ldapDirectory.ldapErrorDescription() );
	}
}



void MainWindow::testLdapGroupTree()
{
	if( testLdapBind( false ) )
	{
		qDebug() << "[TEST][LDAP] Testing group tree";

		LdapDirectory ldapDirectory;
		ldapDirectory.disableFilters();
		int count = ldapDirectory.groups().count();

		reportLdapTreeQueryResult( tr( "group tree" ), count, ldapDirectory.ldapErrorDescription() );
	}
}



void MainWindow::testLdapComputerTree()
{
	if( testLdapBind( false ) )
	{
		qDebug() << "[TEST][LDAP] Testing computer tree";

		LdapDirectory ldapDirectory;
		ldapDirectory.disableFilters();
		int count = ldapDirectory.computers().count();

		reportLdapTreeQueryResult( tr( "computer tree" ), count, ldapDirectory.ldapErrorDescription() );
	}
}



void MainWindow::testLdapUserLoginAttribute()
{
	QString userFilter = QInputDialog::getText( this, tr( "Enter username" ),
										  tr( "Please enter a user login name (wildcards allowed) which to query:") );
	if( userFilter.isEmpty() == false )
	{
		qDebug() << "[TEST][LDAP] Testing user login attribute for" << userFilter;

		LdapDirectory ldapDirectory;
		ldapDirectory.disableFilters();

		reportLdapObjectQueryResults( tr( "user objects" ), tr( "user login attribute" ),
									  ldapDirectory.users( userFilter ), ldapDirectory );
	}
}



void MainWindow::testLdapGroupMemberAttribute()
{
	QString groupFilter = QInputDialog::getText( this, tr( "Enter group name" ),
										  tr( "Please enter a group name whose members to query:") );
	if( groupFilter.isEmpty() == false )
	{
		qDebug() << "[TEST][LDAP] Testing group member attribute for" << groupFilter;

		LdapDirectory ldapDirectory;
		ldapDirectory.disableFilters();

		QStringList groups = ldapDirectory.groups( groupFilter );

		if( groups.isEmpty() == false )
		{
			reportLdapObjectQueryResults( tr( "group members" ), tr( "group member attribute" ),
										  ldapDirectory.groupMembers( groups.first() ), ldapDirectory );
		}
		else
		{
			QMessageBox::warning( this, tr( "Group not found"),
								  tr( "Could not find a group with the name \"%1\". "
									  "Please check the group name or the group "
									  "tree parameter.").arg( groupFilter ) );
		}
	}
}



void MainWindow::testLdapComputerHostNameAttribute()
{
	QString computerName = QInputDialog::getText( this, tr( "Enter computer name" ),
										  tr( "Please enter a computer name whose host name to query:") );
	if( computerName.isEmpty() == false )
	{
		qDebug() << "[TEST][LDAP] Testing computer host name attribute";

		LdapDirectory ldapDirectory;
		ldapDirectory.disableFilters();

		reportLdapObjectQueryResults( tr( "computer objects" ), tr( "computer host name attribute" ),
									  ldapDirectory.computers( computerName ), ldapDirectory );
	}
}



void MainWindow::testLdapUsersFilter()
{
	qDebug() << "[TEST][LDAP] Testing users filter";

	LdapDirectory ldapDirectory;
	int count = ldapDirectory.users().count();

	reportLdapFilterTestResult( tr( "users" ), count, ldapDirectory.ldapErrorDescription() );
}



void MainWindow::testLdapUserGroupsFilter()
{
	qDebug() << "[TEST][LDAP] Testing user groups filter";

	LdapDirectory ldapDirectory;
	int count = ldapDirectory.userGroups().count();

	reportLdapFilterTestResult( tr( "user groups" ), count, ldapDirectory.ldapErrorDescription() );
}



void MainWindow::testLdapComputerGroupsFilter()
{
	qDebug() << "[TEST][LDAP] Testing computer groups filter";

	LdapDirectory ldapDirectory;
	int count = ldapDirectory.computerGroups().count();

	reportLdapFilterTestResult( tr( "computer groups" ), count, ldapDirectory.ldapErrorDescription() );
}



void MainWindow::testLdapComputerPoolAttribute()
{
	QString computerPoolName = QInputDialog::getText( this, tr( "Enter computer pool name" ),
										  tr( "Please enter the name of a computer pool (wildcards allowed):") );
	if( computerPoolName.isEmpty() == false )
	{
		qDebug() << "[TEST][LDAP] Testing computer pool attribute for" << computerPoolName;

		LdapDirectory ldapDirectory;

		reportLdapObjectQueryResults( tr( "computer pools" ), tr( "computer pool attribute" ),
									  ldapDirectory.computerPools( computerPoolName ), ldapDirectory );
	}
}



void MainWindow::testLdapGroupsOfUser()
{
	QString userName = QInputDialog::getText( this, tr( "Enter username" ),
										  tr( "Please enter a user login name whose group memberships to query:") );
	if( userName.isEmpty() == false )
	{
		qDebug() << "[TEST][LDAP] Testing groups of user" << userName;

		LdapDirectory ldapDirectory;

		QStringList userObjects = ldapDirectory.users(userName);

		if( userObjects.isEmpty() == false )
		{
			reportLdapObjectQueryResults( tr( "groups of user" ), tr( "user login attribute or group membership attribute" ),
										  ldapDirectory.groupsOfUser( userObjects.first() ), ldapDirectory );
		}
		else
		{
			QMessageBox::warning( this, tr( "User not found" ),
								  tr( "Could not find a user with the name \"%1\". "
									  "Please check the user name or the user "
									  "tree parameter.").arg( userName ) );
		}
	}
}



void MainWindow::testLdapGroupsOfComputer()
{
	QString computerHostName = QInputDialog::getText( this, tr( "Enter host name" ),
										  tr( "Please enter a computer host name whose group memberships to query:") );
	if( computerHostName.isEmpty() == false )
	{
		qDebug() << "[TEST][LDAP] Testing groups of computer for" << computerHostName;

		LdapDirectory ldapDirectory;

		QStringList computerObjects = ldapDirectory.computers(computerHostName);

		if( computerObjects.isEmpty() == false )
		{
			reportLdapObjectQueryResults( tr( "groups of computer" ), tr( "computer host name attribute or group membership attribute" ),
										  ldapDirectory.groupsOfComputer( computerObjects.first() ), ldapDirectory );
		}
		else
		{
			QMessageBox::warning( this, tr( "Computer not found" ),
								  tr( "Could not find a computer with the host name \"%1\". "
									  "Please check the host name or the computer tree "
									  "tree parameter.").arg( computerHostName ) );
		}
	}
}



void MainWindow::testLdapComputerPoolMembers()
{
	QString computerPoolName = QInputDialog::getText( this, tr( "Enter computer pool name" ),
													  tr( "Please enter the name of a computer pool whose members to query:") );
	if( computerPoolName.isEmpty() == false )
	{
		qDebug() << "[TEST][LDAP] Testing computer pool members for" << computerPoolName;

		LdapDirectory ldapDirectory;
		reportLdapObjectQueryResults( tr( "computer pool members" ),
									  tr( "computer group filter or computer pool member aggregation" ),
									  ldapDirectory.computerPoolMembers( computerPoolName ), ldapDirectory );
	}
}



void MainWindow::testLdapCommonAggregations()
{
	QString objectOne = QInputDialog::getText( this, tr( "Enter first object" ),
										  tr( "Please enter the distinguished name of the first object:" ) );
	if( objectOne.isEmpty() )
	{
		return;
	}

	QString objectTwo = QInputDialog::getText( this, tr( "Enter second object" ),
										  tr( "Please enter the distinguished name of the second object:" ) );
	if( objectTwo.isEmpty() )
	{
		return;
	}

	qDebug() << "[TEST][LDAP] Testing common aggregations of" << ( QStringList() << objectOne << objectTwo );

	LdapDirectory ldapDirectory;

	reportLdapObjectQueryResults( tr( "common aggregations" ),
								  tr( "group membership or computer pool attribute" ),
								  ldapDirectory.commonAggregations( objectOne, objectTwo ), ldapDirectory );
}



void MainWindow::generateBugReportArchive()
{
	FileSystemBrowser fsb( FileSystemBrowser::SaveFile );
	fsb.setShrinkPath( false );
	fsb.setExpandPath( false );
	QString outfile = fsb.exec( QDir::homePath(),
								tr( "Save bug report archive" ),
								tr( "iTALC bug report archive (*.ibra.xml)" ) );
	if( outfile.isEmpty() )
	{
		return;
	}

	if( !outfile.endsWith( ".ibra.xml" ) )
	{
		outfile += ".ibra.xml";
	}

	Configuration::XmlStore bugReportXML(
							Configuration::Store::BugReportArchive, outfile );
	Configuration::Object obj( &bugReportXML );


	// retrieve some basic system information

#ifdef ITALC_BUILD_WIN32

	OSVERSIONINFOEX ovi;
	ovi.dwOSVersionInfoSize = sizeof( ovi );
	GetVersionEx( (LPOSVERSIONINFO) &ovi );

	QString os = "Windows %1 SP%2 (%3.%4.%5)";
	switch( QSysInfo::windowsVersion() )
	{
		case QSysInfo::WV_NT: os = os.arg( "NT 4.0" ); break;
		case QSysInfo::WV_2000: os = os.arg( "2000" ); break;
		case QSysInfo::WV_XP: os = os.arg( "XP" ); break;
		case QSysInfo::WV_VISTA: os = os.arg( "Vista" ); break;
		case QSysInfo::WV_WINDOWS7: os = os.arg( "7" ); break;
		default: os = os.arg( "<unknown>" );
	}

	os = os.arg( ovi.wServicePackMajor ).
			arg( ovi.dwMajorVersion ).
			arg( ovi.dwMinorVersion ).
			arg( ovi.dwBuildNumber );

	const QString machineInfo =
		QProcessEnvironment::systemEnvironment().value( "PROCESSOR_IDENTIFIER" );

#elif defined( ITALC_BUILD_LINUX )

	QFile f( "/etc/lsb-release" );
	f.open( QFile::ReadOnly );

	const QString os = "Linux\n" + f.readAll().trimmed();

	QProcess p;
	p.start( "uname", QStringList() << "-a" );
	p.waitForFinished();
	const QString machineInfo = p.readAll().trimmed();

#endif

#ifdef ITALC_HOST_X86
	const QString buildType = "x86";
#elif defined( ITALC_HOST_X86_64 )
	const QString buildType = "x86_64";
#else
	const QString buildType = "unknown";
#endif
	obj.setValue( "OS", os, "General" );
	obj.setValue( "MachineInfo", machineInfo, "General" );
	obj.setValue( "BuildType", buildType, "General" );
	obj.setValue( "Version", ITALC_VERSION, "General" );


	// add current iTALC configuration
	obj.addSubObject( ItalcCore::config, "Configuration" );


	// compress all log files and encode them as base64
	QStringList paths;
	paths << LocalSystem::Path::expand( ItalcCore::config->logFileDirectory() );
#ifdef ITALC_BUILD_WIN32
	paths << "C:\\Windows\\Temp";
#else
	paths << "/tmp";
#endif
	foreach( const QString &p, paths )
	{
		QDir d( p );
		foreach( const QString &f, d.entryList( QStringList() << "Italc*.log" ) )
		{
			QFile logfile( d.absoluteFilePath( f ) );
			logfile.open( QFile::ReadOnly );
			QByteArray data = qCompress( logfile.readAll() ).toBase64();
			obj.setValue( QFileInfo( logfile ).baseName(), data, "LogFiles" );
		}
	}

	// write the file
	obj.flushStore();

	QMessageBox::information( this, tr( "iTALC bug report archive saved" ),
			tr( "An iTALC bug report archive has been saved to %1. "
				"It includes iTALC log files and information about your "
				"operating system. You can attach it to a bug report." ).
				arg( QDTNS( outfile ) ) );
}



void MainWindow::aboutItalc()
{
	AboutDialog( this ).exec();
}



void MainWindow::closeEvent( QCloseEvent *closeEvent )
{
	if( m_configChanged &&
			QMessageBox::question( this, tr( "Unsaved settings" ),
									tr( "There are unsaved settings. "
										"Quit anyway?" ),
									QMessageBox::Yes | QMessageBox::No ) !=
															QMessageBox::Yes )
	{
		closeEvent->ignore();
		return;
	}

	// make sure to revert the LogonACL
	reset();

	closeEvent->accept();
	QMainWindow::closeEvent( closeEvent );
}




void MainWindow::serviceControlWithProgressBar( const QString &title,
												const QString &arg )
{
	QProcess p;
	p.start( ImcCore::icaFilePath(), QStringList() << arg );
	p.waitForStarted();

	QProgressDialog pd( title, QString(), 0, 0, this );
	pd.setWindowTitle( windowTitle() );

	QProgressBar *b = new QProgressBar( &pd );
	b->setMaximum( 100 );
	b->setTextVisible( false );
	pd.setBar( b );
	b->show();
	pd.setWindowModality( Qt::WindowModal );
	pd.show();

	int j = 0;
	while( p.state() == QProcess::Running )
	{
		QApplication::processEvents();
		b->setValue( ++j % 100 );
		LocalSystem::sleep( 10 );
	}

	updateServiceControl();
}




bool MainWindow::isServiceRunning()
{
#ifdef ITALC_BUILD_WIN32
	SC_HANDLE hsrvmanager = OpenSCManager( NULL, NULL, SC_MANAGER_CONNECT );
	if( !hsrvmanager )
	{
		ilog_failed( "OpenSCManager()" );
		return false;
	}

	SC_HANDLE hservice = OpenService( hsrvmanager, "icas", SERVICE_QUERY_STATUS );
	if( !hservice )
	{
		ilog_failed( "OpenService()" );
		CloseServiceHandle( hsrvmanager );
		return false;
	}

	SERVICE_STATUS status;
	QueryServiceStatus( hservice, &status );

	CloseServiceHandle( hservice );
	CloseServiceHandle( hsrvmanager );

	return( status.dwCurrentState == SERVICE_RUNNING );
#else
	return false;
#endif
}



void MainWindow::reportLdapTreeQueryResult(const QString &name, int count, const QString &errorDescription)
{
	if( count <= 0 )
	{
		QMessageBox::critical( this, tr( "LDAP %1 test failed").arg( name ),
							   tr( "Could not query any entries in configured %1. "
								   "Please check the %1 parameter.\n\n"
								   "%2" ).arg( name, errorDescription ) );
	}
	else
	{
		QMessageBox::information( this, tr( "LDAP %1 test successful" ).arg( name ),
						tr( "The %1 has been queried successfully and "
							"%2 entries were found." ).arg( name ).arg( count ) );
	}
}





void MainWindow::reportLdapObjectQueryResults( const QString &objectsName, const QString& parameterName,
											   const QStringList& results, const LdapDirectory &directory )
{
	if( results.isEmpty() )
	{
		QMessageBox::critical( this, tr( "LDAP %2 test failed").arg( parameterName ),
							   tr( "Could not query any %1. "
								   "Please check the %2 parameter or enter the name of an existing object.\n\n"
								   "%3" ).arg( objectsName, parameterName, directory.ldapErrorDescription() ) );
	}
	else
	{
		QMessageBox::information( this, tr( "LDAP %1 test successful" ).arg( parameterName ),
						tr( "%1 %2 have been queried successfully:\n\n%3" ).
								  arg( results.count() ).
								  arg( objectsName ).
								  arg( formatResultsString( results ) ) );
	}
}





void MainWindow::reportLdapFilterTestResult( const QString &filterObjects, int count, const QString &errorDescription )
{
	if( count <= 0 )
	{
		QMessageBox::critical( this, tr( "LDAP filter test failed"),
							   tr( "Could not query any %1 using the configured filter. "
								   "Please check the LDAP filter for %1.\n\n"
								   "%2" ).arg( filterObjects, errorDescription ) );
	}
	else
	{
		QMessageBox::information( this, tr( "LDAP filter test successful" ),
						tr( "%1 %2 have been queried successfully using the configured filter." ).
								  arg( count ).arg( filterObjects ) );
	}
}



QString MainWindow::formatResultsString( const QStringList &results )
{
	switch( results.count() )
	{
	case 0: return QString();
	case 1: return results.first();
	case 2: return QString( "%1\n%2" ).arg( results[0], results[1] );
	default: break;
	}

	return QString( "%1\n%2\n[...]" ).arg( results[0], results[1] );
}
