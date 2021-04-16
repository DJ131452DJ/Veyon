/*
 * LinuxSessionFunctions.cpp - implementation of LinuxSessionFunctions class
 *
 * Copyright (c) 2020-2021 Tobias Junghans <tobydox@veyon.io>
 *
 * This file is part of Veyon - https://veyon.io
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

#include <QDateTime>
#include <QDBusReply>
#include <QProcessEnvironment>

#include <proc/readproc.h>

#include "LinuxCoreFunctions.h"
#include "LinuxSessionFunctions.h"
#include "PlatformSessionManager.h"


LinuxSessionFunctions::SessionId LinuxSessionFunctions::currentSessionId()
{
	return PlatformSessionManager::resolveSessionId( currentSessionPath() );
}



QVariant LinuxSessionFunctions::getSessionProperty( const QString& session, const QString& property )
{
	QDBusInterface loginManager( QStringLiteral("org.freedesktop.login1"),
								 session,
								 QStringLiteral("org.freedesktop.DBus.Properties"),
								 QDBusConnection::systemBus() );

	const QDBusReply<QDBusVariant> reply = loginManager.call( QStringLiteral("Get"),
															  QStringLiteral("org.freedesktop.login1.Session"),
															  property );

	if( reply.isValid() == false )
	{
		vCritical() << "Could not query session property" << property << reply.error().message();
		return {};
	}

	return reply.value().variant();
}



int LinuxSessionFunctions::getSessionLeaderPid( const QString& session )
{
	const auto leader = getSessionProperty( session, QStringLiteral("Leader") );

	if( leader.isNull() )
	{
		return -1;
	}

	return leader.toInt();
}



qint64 LinuxSessionFunctions::getSessionUptimeSeconds( const QString& session )
{
	const auto sessionUptimeUsec = getSessionProperty( session, QStringLiteral("Timestamp") );

	if( sessionUptimeUsec.isNull() )
	{
		return -1;
	}

#if QT_VERSION < QT_VERSION_CHECK(5, 8, 0)
	const auto currentTimestamp = QDateTime::currentMSecsSinceEpoch() / 1000;
#else
	const auto currentTimestamp = QDateTime::currentSecsSinceEpoch();
#endif

	return currentTimestamp - qint64( sessionUptimeUsec.toLongLong() / ( 1000 * 1000 ) );
}



LinuxSessionFunctions::Class LinuxSessionFunctions::getSessionClass( const QString& session )
{
	const auto sessionClass = getSessionProperty( session, QStringLiteral("Class") ).toString();
	if( sessionClass == QLatin1String("user") )
	{
		return Class::User;
	}
	else if( sessionClass == QLatin1String("greeter") )
	{
		return Class::Greeter;
	}
	else if( sessionClass == QLatin1String("lock-screen") )
	{
		return Class::LockScreen;
	}

	return Class::Unknown;
}



QString LinuxSessionFunctions::getSessionType( const QString& session )
{
	return getSessionProperty( session, QStringLiteral("Type") ).toString();
}



QString LinuxSessionFunctions::getSessionId( const QString& session )
{
	return getSessionProperty( session, QStringLiteral("Id") ).toString();
}



LinuxSessionFunctions::State LinuxSessionFunctions::getSessionState( const QString& session )
{
	static const QMap<QString, State> stateMap{
		{ QStringLiteral("offline"), State::Offline },
		{ QStringLiteral("lingering"), State::Lingering },
		{ QStringLiteral("online"), State::Online },
		{ QStringLiteral("active"), State::Active },
		{ QStringLiteral("opening"), State::Opening },
		{ QStringLiteral("closing"), State::Closing },
	};

	const auto stateString = getSessionProperty( session, QStringLiteral("State") ).toString();
	const auto state = stateMap.value( stateString, State::Unknown );
	if( state == State::Unknown )
	{
		vDebug() << stateString;
	}

	return state;
}



LinuxSessionFunctions::LoginDBusSessionSeat LinuxSessionFunctions::getSessionSeat( const QString& session )
{
	const auto seatArgument = getSessionProperty( session, QStringLiteral("Seat") ).value<QDBusArgument>();

	LoginDBusSessionSeat seat;
	seatArgument.beginStructure();
	seatArgument >> seat.id;
	seatArgument >> seat.path;
	seatArgument.endStructure();

	return seat;
}



QProcessEnvironment LinuxSessionFunctions::getSessionEnvironment( int sessionLeaderPid )
{
	QProcessEnvironment sessionEnv;

	LinuxCoreFunctions::forEachChildProcess(
		[&sessionEnv]( proc_t* procInfo ) {
			if( procInfo->environ != nullptr )
			{
				for( int i = 0; procInfo->environ[i]; ++i )
				{
					const auto env = QString::fromUtf8( procInfo->environ[i] );
					const auto separatorPos = env.indexOf( QLatin1Char('=') );
					if( separatorPos > 0 )
					{
						sessionEnv.insert( env.left( separatorPos ), env.mid( separatorPos+1 ) );
					}
				}

				return true;
			}

			return false;
		},
		sessionLeaderPid, PROC_FILLENV, true );

	return sessionEnv;
}



QString LinuxSessionFunctions::currentSessionType() const
{
	const auto env = QProcessEnvironment::systemEnvironment();

	if( env.contains( QStringLiteral("WAYLAND_DISPLAY") ) )
	{
		return QStringLiteral("wayland");
	}
	else if( env.contains( QStringLiteral("DISPLAY") ) )
	{
		return QStringLiteral("x11");
	}

	return getSessionType( currentSessionPath() );
}



QString LinuxSessionFunctions::currentSessionPath()
{
	const auto xdgSessionId = QProcessEnvironment::systemEnvironment().value( xdgSessionIdEnvVarName() );
	if( xdgSessionId.isEmpty() )
	{
		return QStringLiteral("/org/freedesktop/login1/session/self");
	}

	return QStringLiteral("/org/freedesktop/login1/session/%1").arg( xdgSessionId );
}



QStringList LinuxSessionFunctions::listSessions()
{
	QStringList sessions;

	const QDBusReply<QDBusArgument> reply = LinuxCoreFunctions::systemdLoginManager()->call( QStringLiteral("ListSessions") );

	if( reply.isValid() )
	{
		const auto data = reply.value();

		data.beginArray();
		while( data.atEnd() == false )
		{
			LoginDBusSession session;

			data.beginStructure();
			data >> session.id >> session.uid >> session.name >> session.seatId >> session.path;
			data.endStructure();

			sessions.append( session.path.path() );
		}
		return sessions;
	}

	vCritical() << "Could not query sessions:" << reply.error().message();

	return sessions;
}
